#include "innertube.hpp"
#include "../settings/settings_store.hpp"
#include "../util/log.hpp"

#include <curl/curl.h>

#include <jansson.h>

#include <atomic>
#include <chrono>
#include <cstdio>
#include <memory>
#include <string>

namespace ssnx::yt
{
namespace
{

constexpr const char* kApiKey = "AIzaSyAO_FJ2SlqU8Q4STEHLGCilw_Y9_11qcW8";
constexpr const char* kMusicHost = "https://music.youtube.com/youtubei/v1/";
constexpr const char* kWebRemixVersion = "1.20260707.12.00";
constexpr const char* kWebRemixId = "67";
constexpr const char* kUserAgent =
    "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 "
    "(KHTML, like Gecko) Chrome/141.0.0.0 Safari/537.36";

using JsonPtr = std::unique_ptr<json_t, decltype(&json_decref)>;

size_t WriteCallback(char* ptr, size_t size, size_t nmemb, void* userdata)
{
    auto* out = static_cast<std::string*>(userdata);
    out->append(ptr, size * nmemb);
    return size * nmemb;
}

size_t FileWriteCallback(char* ptr, size_t size, size_t nmemb, void* userdata)
{
    auto* fp = static_cast<FILE*>(userdata);
    return fwrite(ptr, size, nmemb, fp);
}

std::string GetString(json_t* object, const char* key)
{
    if (!json_is_object(object))
        return "";
    json_t* value = json_object_get(object, key);
    if (!json_is_string(value))
        return "";
    const char* raw = json_string_value(value);
    return raw ? raw : "";
}

std::string RunsText(json_t* flex_column)
{
    // flexColumns[i].musicResponsiveListItemFlexColumnRenderer.text.runs[].text
    json_t* inner = json_object_get(flex_column, "musicResponsiveListItemFlexColumnRenderer");
    json_t* text = inner ? json_object_get(inner, "text") : nullptr;
    json_t* runs = text ? json_object_get(text, "runs") : nullptr;
    if (!json_is_array(runs))
        return "";
    std::string out;
    size_t index = 0;
    json_t* run = nullptr;
    json_array_foreach(runs, index, run)
    {
        out += GetString(run, "text");
    }
    return out;
}

std::pair<std::string, bool> ParseArtistAndType(const std::string& raw_subtitle)
{
    std::vector<std::string> parts;
    std::string current;
    for (size_t i = 0; i < raw_subtitle.size(); )
    {
        unsigned char c = static_cast<unsigned char>(raw_subtitle[i]);
        if (c == 0xE2 && i + 2 < raw_subtitle.size() &&
            static_cast<unsigned char>(raw_subtitle[i+1]) == 0x80 &&
            static_cast<unsigned char>(raw_subtitle[i+2]) == 0xA2) // '•' (U+2022)
        {
            parts.push_back(current);
            current.clear();
            i += 3;
        }
        else if (c == 0xC2 && i + 1 < raw_subtitle.size() &&
                 static_cast<unsigned char>(raw_subtitle[i+1]) == 0xB7) // '·' (U+00B7)
        {
            parts.push_back(current);
            current.clear();
            i += 2;
        }
        else
        {
            current += raw_subtitle[i];
            i++;
        }
    }
    if (!current.empty())
        parts.push_back(current);

    auto trim = [](std::string s) {
        while (!s.empty() && std::isspace(static_cast<unsigned char>(s.front())))
            s.erase(s.begin());
        while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back())))
            s.pop_back();
        return s;
    };

    std::vector<std::string> clean_parts;
    for (auto& p : parts)
    {
        std::string t = trim(p);
        if (!t.empty())
            clean_parts.push_back(t);
    }

    bool is_song = false;
    std::string artist;

    if (!clean_parts.empty())
    {
        std::string first_lower = clean_parts[0];
        std::transform(first_lower.begin(), first_lower.end(), first_lower.begin(), ::tolower);

        if (first_lower == "song" || first_lower == "audio" || first_lower == "track")
        {
            is_song = true;
            if (clean_parts.size() > 1)
                artist = clean_parts[1];
        }
        else if (first_lower == "video" || first_lower == "music video" || first_lower == "episode")
        {
            is_song = false;
            if (clean_parts.size() > 1)
                artist = clean_parts[1];
        }
        else
        {
            artist = clean_parts[0];
        }
    }

    if (artist.empty() && !clean_parts.empty())
        artist = clean_parts[0];

    // Strip " - Topic" suffix (case-insensitive) - Topic channels are official studio audio
    if (artist.size() >= 8)
    {
        std::string suffix = artist.substr(artist.size() - 8);
        std::transform(suffix.begin(), suffix.end(), suffix.begin(), ::tolower);
        if (suffix == " - topic")
        {
            artist.resize(artist.size() - 8);
            artist = trim(artist);
            is_song = true;
        }
    }

    std::string check_lower = artist;
    std::transform(check_lower.begin(), check_lower.end(), check_lower.begin(), ::tolower);
    if (check_lower == "song" || check_lower == "video")
        artist.clear();

    return {artist, is_song};
}

std::optional<Track> ParseCardShelf(json_t* card)
{
    if (!json_is_object(card))
        return std::nullopt;

    Track track;

    // Title & Video ID from title runs
    json_t* title_obj = json_object_get(card, "title");
    json_t* title_runs = title_obj ? json_object_get(title_obj, "runs") : nullptr;
    if (json_is_array(title_runs))
    {
        size_t idx = 0;
        json_t* r = nullptr;
        json_array_foreach(title_runs, idx, r)
        {
            track.title += GetString(r, "text");
            if (track.video_id.empty())
            {
                json_t* nav = json_object_get(r, "navigationEndpoint");
                json_t* watch = nav ? json_object_get(nav, "watchEndpoint") : nullptr;
                track.video_id = GetString(watch, "videoId");
            }
        }
    }

    // Fallback Video ID from onTap
    if (track.video_id.empty())
    {
        json_t* on_tap = json_object_get(card, "onTap");
        json_t* watch = on_tap ? json_object_get(on_tap, "watchEndpoint") : nullptr;
        track.video_id = GetString(watch, "videoId");
    }

    // Fallback Video ID from thumbnailOverlay play button
    if (track.video_id.empty())
    {
        json_t* overlay = json_object_get(card, "thumbnailOverlay");
        json_t* content = overlay ? json_object_get(overlay, "musicItemThumbnailOverlayRenderer") : nullptr;
        content = content ? json_object_get(content, "content") : nullptr;
        json_t* play_btn = content ? json_object_get(content, "musicPlayButtonRenderer") : nullptr;
        json_t* ep = play_btn ? json_object_get(play_btn, "playNavigationEndpoint") : nullptr;
        json_t* watch = ep ? json_object_get(ep, "watchEndpoint") : nullptr;
        track.video_id = GetString(watch, "videoId");
    }

    if (track.video_id.empty() || track.title.empty())
        return std::nullopt;

    // Subtitle & Artist parsing
    json_t* sub_obj = json_object_get(card, "subtitle");
    json_t* sub_runs = sub_obj ? json_object_get(sub_obj, "runs") : nullptr;
    std::vector<std::string> parts;
    std::string current;
    if (json_is_array(sub_runs))
    {
        size_t idx = 0;
        json_t* r = nullptr;
        json_array_foreach(sub_runs, idx, r)
        {
            std::string t = GetString(r, "text");
            track.subtitle += t;

            std::string trimmed = t;
            while (!trimmed.empty() && std::isspace(static_cast<unsigned char>(trimmed.front())))
                trimmed.erase(trimmed.begin());
            while (!trimmed.empty() && std::isspace(static_cast<unsigned char>(trimmed.back())))
                trimmed.pop_back();

            if (trimmed == "•" || trimmed == "·")
            {
                if (!current.empty())
                {
                    parts.push_back(current);
                    current.clear();
                }
            }
            else
            {
                current += t;
            }
        }
        if (!current.empty())
            parts.push_back(current);
    }

    auto trim = [](std::string s) {
        while (!s.empty() && std::isspace(static_cast<unsigned char>(s.front())))
            s.erase(s.begin());
        while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back())))
            s.pop_back();
        return s;
    };

    std::string parsed_artist;
    if (!parts.empty())
    {
        std::string first_clean = trim(parts[0]);
        std::string first_lower = first_clean;
        std::transform(first_lower.begin(), first_lower.end(), first_lower.begin(), ::tolower);
        if (first_lower == "song" || first_lower == "video" || first_lower == "audio" || first_lower == "track")
        {
            if (parts.size() > 1)
                parsed_artist = trim(parts[1]);
        }
        else
        {
            parsed_artist = first_clean;
        }
    }

    if (parsed_artist.size() >= 8)
    {
        std::string suffix = parsed_artist.substr(parsed_artist.size() - 8);
        std::transform(suffix.begin(), suffix.end(), suffix.begin(), ::tolower);
        if (suffix == " - topic")
        {
            parsed_artist.resize(parsed_artist.size() - 8);
            parsed_artist = trim(parsed_artist);
        }
    }

    std::string check_lower = parsed_artist;
    std::transform(check_lower.begin(), check_lower.end(), check_lower.begin(), ::tolower);
    if (check_lower == "song" || check_lower == "video")
        parsed_artist.clear();

    track.artist = parsed_artist.empty() ? track.subtitle : parsed_artist;

    // Thumbnail
    json_t* thumbnail = json_object_get(card, "thumbnail");
    json_t* mtr = thumbnail ? json_object_get(thumbnail, "musicThumbnailRenderer") : nullptr;
    json_t* thumb = mtr ? json_object_get(mtr, "thumbnail") : nullptr;
    json_t* thumbs = thumb ? json_object_get(thumb, "thumbnails") : nullptr;
    if (json_is_array(thumbs) && json_array_size(thumbs) > 0)
        track.thumb_url = GetString(json_array_get(thumbs, json_array_size(thumbs) - 1), "url");

    return track;
}

void CollectItems(json_t* node, std::vector<Track>& songs, std::vector<Track>& videos, std::optional<Track>& top_card)
{
    if (songs.size() + videos.size() >= 40)
        return;
    if (json_is_array(node))
    {
        size_t index = 0;
        json_t* entry = nullptr;
        json_array_foreach(node, index, entry)
        {
            CollectItems(entry, songs, videos, top_card);
            if (songs.size() + videos.size() >= 40)
                return;
        }
        return;
    }
    if (!json_is_object(node))
        return;

    // Top Result Card (featured master match)
    json_t* card = json_object_get(node, "musicCardShelfRenderer");
    if (card)
    {
        if (!top_card.has_value())
            top_card = ParseCardShelf(card);

        json_t* card_contents = json_object_get(card, "contents");
        if (json_is_array(card_contents))
        {
            size_t c_idx = 0;
            json_t* c_entry = nullptr;
            json_array_foreach(card_contents, c_idx, c_entry)
            {
                CollectItems(c_entry, songs, videos, top_card);
            }
        }
        return;
    }

    json_t* item = json_object_get(node, "musicResponsiveListItemRenderer");
    if (item)
    {
        Track track;
        json_t* columns = json_object_get(item, "flexColumns");
        json_t* col1_runs = nullptr;
        if (json_is_array(columns) && json_array_size(columns) >= 1)
        {
            track.title = RunsText(json_array_get(columns, 0));
            if (json_array_size(columns) >= 2)
            {
                json_t* col1 = json_array_get(columns, 1);
                track.subtitle = RunsText(col1);
                json_t* inner = json_object_get(col1, "musicResponsiveListItemFlexColumnRenderer");
                json_t* text = inner ? json_object_get(inner, "text") : nullptr;
                col1_runs = text ? json_object_get(text, "runs") : nullptr;
            }
        }

        bool is_song = false;
        std::string parsed_artist;

        // Parse runs directly from JSON for highest accuracy
        std::vector<std::string> run_texts;
        if (json_is_array(col1_runs))
        {
            size_t r_idx = 0;
            json_t* r_entry = nullptr;
            json_array_foreach(col1_runs, r_idx, r_entry)
            {
                std::string t = GetString(r_entry, "text");
                while (!t.empty() && std::isspace(static_cast<unsigned char>(t.front())))
                    t.erase(t.begin());
                while (!t.empty() && std::isspace(static_cast<unsigned char>(t.back())))
                    t.pop_back();
                if (t != "•" && t != "·" && t != "-" && !t.empty())
                    run_texts.push_back(t);
            }
        }

        if (!run_texts.empty())
        {
            std::string first_lower = run_texts[0];
            std::transform(first_lower.begin(), first_lower.end(), first_lower.begin(), ::tolower);
            if (first_lower == "song" || first_lower == "audio" || first_lower == "track")
            {
                is_song = true;
                if (run_texts.size() > 1)
                    parsed_artist = run_texts[1];
            }
            else if (first_lower == "video" || first_lower == "music video" || first_lower == "episode")
            {
                is_song = false;
                if (run_texts.size() > 1)
                    parsed_artist = run_texts[1];
            }
            else
            {
                parsed_artist = run_texts[0];
            }
        }

        if (parsed_artist.empty())
        {
            auto [pa, s] = ParseArtistAndType(track.subtitle);
            parsed_artist = pa;
            if (s)
                is_song = true;
        }

        // If item is inside a card shelf and lacks artist name, inherit from card
        if (parsed_artist.empty() && top_card.has_value() && !top_card->artist.empty())
        {
            parsed_artist = top_card->artist;
        }

        // Overlay watchEndpoint & config
        json_t* overlay = json_object_get(item, "overlay");
        json_t* content = overlay
            ? json_object_get(overlay, "musicItemThumbnailOverlayRenderer")
            : nullptr;
        content = content ? json_object_get(content, "content") : nullptr;
        json_t* play_button = content
            ? json_object_get(content, "musicPlayButtonRenderer")
            : nullptr;
        json_t* endpoint = play_button
            ? json_object_get(play_button, "playNavigationEndpoint")
            : nullptr;
        json_t* watch = endpoint ? json_object_get(endpoint, "watchEndpoint") : nullptr;
        track.video_id = GetString(watch, "videoId");
        if (track.video_id.empty())
        {
            json_t* pid = json_object_get(item, "playlistItemData");
            track.video_id = GetString(pid, "videoId");
        }

        // Check if watchEndpoint specifies official audio track (ATV = Art Track Video)
        if (watch)
        {
            json_t* configs = json_object_get(watch, "watchEndpointMusicSupportedConfigs");
            json_t* mcfg = configs ? json_object_get(configs, "watchEndpointMusicConfig") : nullptr;
            if (mcfg)
            {
                std::string mvt = GetString(mcfg, "musicVideoType");
                if (mvt == "MUSIC_VIDEO_TYPE_ATV" || mvt == "MUSIC_VIDEO_TYPE_OFFICIAL_SOURCE_MUSIC")
                    is_song = true;
            }
        }

        // Strip " - Topic"
        if (parsed_artist.size() >= 8)
        {
            std::string suffix = parsed_artist.substr(parsed_artist.size() - 8);
            std::transform(suffix.begin(), suffix.end(), suffix.begin(), ::tolower);
            if (suffix == " - topic")
            {
                parsed_artist.resize(parsed_artist.size() - 8);
                while (!parsed_artist.empty() && std::isspace(static_cast<unsigned char>(parsed_artist.back())))
                    parsed_artist.pop_back();
                is_song = true;
            }
        }

        // Safeguard: never let "Song" or "Video" be the displayed artist
        std::string check_lower = parsed_artist;
        std::transform(check_lower.begin(), check_lower.end(), check_lower.begin(), ::tolower);
        if (check_lower == "song" || check_lower == "video")
            parsed_artist.clear();

        track.artist = parsed_artist;

        json_t* thumbnail = json_object_get(item, "thumbnail");
        json_t* mtr = thumbnail
            ? json_object_get(thumbnail, "musicThumbnailRenderer")
            : nullptr;
        json_t* thumb = mtr ? json_object_get(mtr, "thumbnail") : nullptr;
        json_t* thumbs = thumb ? json_object_get(thumb, "thumbnails") : nullptr;
        if (json_is_array(thumbs) && json_array_size(thumbs) > 0)
            track.thumb_url = GetString(json_array_get(thumbs, json_array_size(thumbs) - 1), "url");

        if (!track.title.empty() && !track.video_id.empty())
        {
            if (is_song)
                songs.push_back(std::move(track));
            else
                videos.push_back(std::move(track));
        }
        return;
    }

    const char* key = nullptr;
    json_t* value = nullptr;
    json_object_foreach(node, key, value)
    {
        CollectItems(value, songs, videos, top_card);
        if (songs.size() + videos.size() >= 40)
            return;
    }
}

} // namespace

std::string InnertubeClient::Post(const std::string& endpoint, const std::string& body,
                                  const std::string& client_id, const std::string& client_version)
{
    std::string response;
    CURL* curl = curl_easy_init();
    if (!curl)
        return response;

    const std::string url =
        std::string(kMusicHost) + endpoint + "?key=" + kApiKey + "&prettyPrint=false";

    struct curl_slist* headers = nullptr;
    headers = curl_slist_append(headers, "Content-Type: application/json");
    const std::string name_header = "X-YouTube-Client-Name: " + client_id;
    const std::string version_header = "X-YouTube-Client-Version: " + client_version;
    headers = curl_slist_append(headers, name_header.c_str());
    headers = curl_slist_append(headers, version_header.c_str());
    headers = curl_slist_append(headers, "X-Goog-Api-Format-Version: 1");
    headers = curl_slist_append(headers, "Origin: https://music.youtube.com");
    headers = curl_slist_append(headers, "Referer: https://music.youtube.com/");

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, kUserAgent);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body.c_str());
    curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, static_cast<long>(body.size()));
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 25L);

    const CURLcode rc = curl_easy_perform(curl);
    long http_code = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);

    if (rc != CURLE_OK || http_code != 200)
        response.clear();
    return response;
}

namespace
{

std::string g_cached_visitor_data;

std::string GenerateWebVisitorId()
{
    static const char kAlphabet[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";
    static std::atomic<uint64_t> sequence {0};
    uint64_t state = static_cast<uint64_t>(
        std::chrono::high_resolution_clock::now().time_since_epoch().count())
        ^ (++sequence * 0x9e3779b97f4a7c15ULL);

    std::string result(11, 'A');
    for (char& ch : result)
    {
        state ^= state << 13U;
        state ^= state >> 7U;
        state ^= state << 17U;
        ch = kAlphabet[state & 0x3fU];
    }
    return result;
}

std::string FetchWebVisitorData()
{
    if (!g_cached_visitor_data.empty())
        return g_cached_visitor_data;

    std::string response;
    CURL* curl = curl_easy_init();
    if (!curl)
        return "";

    const std::string cookie = "PREF=tz=Asia.Seoul;VISITOR_INFO1_LIVE=" + GenerateWebVisitorId() + ";";
    struct curl_slist* headers = nullptr;
    headers = curl_slist_append(headers, "Accept-Language: en-US");
    headers = curl_slist_append(headers, "Accept: */*");
    headers = curl_slist_append(headers, "Referer: https://www.youtube.com/sw.js");
    const std::string cookie_header = "Cookie: " + cookie;
    headers = curl_slist_append(headers, cookie_header.c_str());

    curl_easy_setopt(curl, CURLOPT_URL, "https://www.youtube.com/sw.js_data");
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, kUserAgent);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 15L);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);

    const CURLcode rc = curl_easy_perform(curl);
    long http_code = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);

    if (rc != CURLE_OK || http_code != 200 || response.empty())
        return "";

    const size_t json_start = response.find('[');
    if (json_start == std::string::npos)
        return "";

    json_error_t error {};
    JsonPtr data(json_loads(response.substr(json_start).c_str(), 0, &error), &json_decref);
    if (!data || !json_is_array(data.get()))
        return "";

    // Array structure: data[0][2][0][0][13]
    json_t* e0 = json_array_get(data.get(), 0);
    json_t* e2 = json_is_array(e0) ? json_array_get(e0, 2) : nullptr;
    json_t* e2_0 = json_is_array(e2) ? json_array_get(e2, 0) : nullptr;
    json_t* e2_0_0 = json_is_array(e2_0) ? json_array_get(e2_0, 0) : nullptr;
    if (json_is_array(e2_0_0) && json_array_size(e2_0_0) > 13)
    {
        json_t* v = json_array_get(e2_0_0, 13);
        if (json_is_string(v))
        {
            g_cached_visitor_data = json_string_value(v);
            return g_cached_visitor_data;
        }
    }
    return "";
}

JsonPtr PlayerBody(const std::string& client_name, const std::string& client_version,
                   const std::string& video_id, const std::string& visitor_data,
                   bool ipad, int android_sdk = 30)
{
    JsonPtr body(json_object(), &json_decref);
    JsonPtr context(json_object(), &json_decref);
    JsonPtr client(json_object(), &json_decref);
    json_object_set_new(client.get(), "clientName", json_string(client_name.c_str()));
    json_object_set_new(client.get(), "clientVersion", json_string(client_version.c_str()));
    if (client_name == "VISIONOS")
    {
        json_object_set_new(client.get(), "osName", json_string("visionOS"));
        json_object_set_new(client.get(), "osVersion", json_string("26.5.23O471"));
        json_object_set_new(client.get(), "deviceMake", json_string("Apple"));
        json_object_set_new(client.get(), "deviceModel", json_string("RealityDevice17,1"));
    }
    else if (ipad)
    {
        json_object_set_new(client.get(), "osName", json_string("iPadOS"));
        json_object_set_new(client.get(), "osVersion", json_string("17.7.10.21H450"));
        json_object_set_new(client.get(), "deviceMake", json_string("Apple"));
        json_object_set_new(client.get(), "deviceModel", json_string("iPad7,6"));
    }
    else if (client_name == "ANDROID_VR")
    {
        json_object_set_new(client.get(), "osName", json_string("Android"));
        json_object_set_new(client.get(), "osVersion", json_string("12L"));
        json_object_set_new(client.get(), "deviceMake", json_string("Oculus"));
        json_object_set_new(client.get(), "deviceModel", json_string("Quest 3"));
        json_object_set_new(client.get(), "androidSdkVersion", json_integer(android_sdk > 0 ? android_sdk : 32));
    }
    else
    {
        json_object_set_new(client.get(), "osName", json_string("Android"));
        json_object_set_new(client.get(), "osVersion", json_string("15"));
        json_object_set_new(client.get(), "deviceMake", json_string("Google"));
        json_object_set_new(client.get(), "deviceModel", json_string("Pixel 9 Pro"));
        json_object_set_new(client.get(), "androidSdkVersion", json_integer(android_sdk > 0 ? android_sdk : 35));
    }
    json_object_set_new(client.get(), "hl", json_string("en"));
    json_object_set_new(client.get(), "gl", json_string("US"));
    if (!visitor_data.empty())
        json_object_set_new(client.get(), "visitorData", json_string(visitor_data.c_str()));
    json_object_set_new(context.get(), "client", json_incref(client.get()));
    json_object_set_new(body.get(), "context", json_incref(context.get()));
    json_object_set_new(body.get(), "videoId", json_string(video_id.c_str()));
    json_object_set_new(body.get(), "contentCheckOk", json_true());
    json_object_set_new(body.get(), "racyCheckOk", json_true());
    return body;
}

bool PickAudio(json_t* root, ResolvedAudio& out, const std::string& via_prefix)
{
    json_t* streaming = json_object_get(root, "streamingData");
    json_t* adaptive = streaming ? json_object_get(streaming, "adaptiveFormats") : nullptr;
    if (!json_is_array(adaptive))
        return false;

    const auto quality_pref = settings::SettingsStore::Instance().GetStreamQuality();
    auto itag_score = [quality_pref](int itag) {
        if (quality_pref == settings::StreamQuality::OPUS_HIGH)
        {
            switch (itag)
            {
                case 251: return 100; // Opus ~160k
                case 250: return 85;  // Opus ~70k
                case 249: return 75;  // Opus ~50k
                case 140: return 70;  // AAC ~128k
                case 139: return 60;  // AAC ~48k
                default: return 0;
            }
        }
        else if (quality_pref == settings::StreamQuality::M4A_MED)
        {
            switch (itag)
            {
                case 140: return 100; // AAC ~128k
                case 139: return 85;  // AAC ~48k
                case 251: return 70;  // Opus ~160k
                case 250: return 60;  // Opus ~70k
                default: return 0;
            }
        }
        else if (quality_pref == settings::StreamQuality::LOW)
        {
            switch (itag)
            {
                case 249: return 100; // Opus ~50k
                case 250: return 90;  // Opus ~70k
                case 139: return 80;  // AAC ~48k
                case 599: return 70;  // m4a ~32k
                case 600: return 60;  // opus ~32k
                case 140: return 40;  // AAC ~128k
                case 251: return 30;  // Opus ~160k
                default: return 0;
            }
        }
        // Default AUTO: 251 -> 140 -> 250 -> 249
        switch (itag)
        {
            case 251: return 100; // Opus ~160k
            case 140: return 90;  // AAC ~128k
            case 250: return 80;  // Opus ~70k
            case 249: return 70;  // Opus ~50k
            case 139: return 60;  // AAC ~48k
            case 599: return 50;  // m4a ~32k
            case 600: return 40;  // opus ~32k
            default: return 0;
        }
    };

    json_t* best = nullptr;
    int best_score = -1;
    int best_itag = 0;
    size_t index = 0;
    json_t* entry = nullptr;
    json_array_foreach(adaptive, index, entry)
    {
        json_t* itag_value = json_object_get(entry, "itag");
        const int itag = json_is_integer(itag_value)
            ? static_cast<int>(json_integer_value(itag_value))
            : 0;
        const int score = itag_score(itag);
        if (score <= 0)
            continue;
        if (score > best_score)
        {
            best = entry;
            best_score = score;
            best_itag = itag;
        }
    }
    if (!best)
        return false;

    const std::string url = GetString(best, "url");
    if (url.empty())
        return false;
    out.url = url;
    out.via = via_prefix + "/" + std::to_string(best_itag);
    out.ext = (best_itag == 140 || best_itag == 139 || best_itag == 599) ? "m4a" : "opus";
    return true;
}

} // namespace

std::vector<Track> InnertubeClient::Search(const std::string& query)
{
    last_error_.clear();
    std::vector<Track> tracks;

    JsonPtr body(json_object(), &json_decref);
    JsonPtr context(json_object(), &json_decref);
    JsonPtr client(json_object(), &json_decref);
    json_object_set_new(client.get(), "clientName", json_string("WEB_REMIX"));
    json_object_set_new(client.get(), "clientVersion", json_string(kWebRemixVersion));
    json_object_set_new(client.get(), "hl", json_string("en"));
    json_object_set_new(client.get(), "gl", json_string("US"));
    json_object_set_new(context.get(), "client", json_incref(client.get()));
    json_object_set_new(body.get(), "context", json_incref(context.get()));
    json_object_set_new(body.get(), "query", json_string(query.c_str()));

    char* dump = json_dumps(body.get(), 0);
    if (!dump)
    {
        last_error_ = "json build failed";
        return tracks;
    }
    const std::string payload = dump;
    free(dump);

    const std::string response = Post("search", payload, kWebRemixId, kWebRemixVersion);
    if (response.empty())
    {
        last_error_ = "search HTTP failed";
        return tracks;
    }

    json_error_t error {};
    JsonPtr root(json_loads(response.c_str(), 0, &error), &json_decref);
    if (!root)
    {
        last_error_ = std::string("json parse: ") + error.text;
        return tracks;
    }

    std::optional<Track> top_card;
    std::vector<Track> songs;
    std::vector<Track> videos;
    CollectItems(root.get(), songs, videos, top_card);

    if (top_card.has_value())
        tracks.push_back(std::move(*top_card));

    for (auto& s : songs)
    {
        if (top_card.has_value() && s.video_id == top_card->video_id)
            continue;
        tracks.push_back(std::move(s));
    }
    for (auto& v : videos)
    {
        if (top_card.has_value() && v.video_id == top_card->video_id)
            continue;
        if (tracks.size() >= 25)
            break;
        tracks.push_back(std::move(v));
    }

    if (tracks.empty())
        last_error_ = "no results parsed";
    return tracks;
}

ResolvedAudio InnertubeClient::ResolveAudio(const std::string& video_id)
{
    last_error_.clear();
    ResolvedAudio out;

    struct PlayerClient
    {
        const char* name;
        const char* version;
        const char* id;
        bool ipad;
        int android_sdk;
        const char* via;
        const char* user_agent;
    };
    // Prioritize clients that yield unthrottled audio streams without auth
    static const PlayerClient kClients[] = {
        {"VISIONOS", "1.02", "101", false, 0, "VISIONOS",
         "com.google.ios.youtube/1.02 (RealityDevice17,1; U; CPU visionOS 26_5 like Mac OS X)"},
        {"ANDROID_VR", "1.65.10", "28", false, 32, "ANDROID_VR",
         "com.google.android.apps.youtube.vr.oculus/1.65.10 (Linux; U; Android 12L; eureka-user Build/SQ3A.220605.009.A1) gzip"},
        {"IOS", "21.03.3", "5", true, 0, "IOS",
         "com.google.ios.youtube/21.03.3 (iPad7,6; U; CPU iPadOS 17_7_10 like Mac OS X; en-US)"},
        {"ANDROID", "20.10.38", "3", false, 30, "ANDROID",
         "com.google.android.youtube/20.10.38 (Linux; U; Android 11) gzip"},
        {"ANDROID_MUSIC", "8.39.42", "21", false, 35, "ANDROID_MUSIC",
         "com.google.android.apps.youtube.music/8.39.42 (Linux; U; Android 15; en_US; Pixel 9 Pro; Build/AP4A.250205.002) gzip"},
    };

    auto rlog = [](const std::string& line) {
        ssnx::Log(line);
    };

    const std::string visitor_data = FetchWebVisitorData();
    if (visitor_data.empty())
        rlog("resolve: visitorData bootstrap empty");
    else
        rlog("resolve: visitorData ready (" + std::to_string(visitor_data.size()) + " chars)");

    for (const auto& candidate : kClients)
    {
        rlog(std::string("resolve: trying ") + candidate.via);
        JsonPtr body = PlayerBody(candidate.name, candidate.version, video_id, visitor_data,
                                  candidate.ipad, candidate.android_sdk);
        char* dump = json_dumps(body.get(), 0);
        if (!dump)
            continue;
        const std::string payload = dump;
        free(dump);

        const std::string response = Post("player", payload, candidate.id, candidate.version);
        if (response.empty())
        {
            last_error_ = std::string(candidate.via) + ": player HTTP failed";
            rlog(std::string("resolve: ") + last_error_);
            continue;
        }

        json_error_t error {};
        JsonPtr root(json_loads(response.c_str(), 0, &error), &json_decref);
        if (!root)
        {
            last_error_ = std::string(candidate.via) + ": json parse failed";
            rlog(std::string("resolve: ") + last_error_);
            continue;
        }

        json_t* status = json_object_get(root.get(), "playabilityStatus");
        if (GetString(status, "status") != "OK")
        {
            last_error_ = std::string(candidate.via) + ": " +
                GetString(status, "status") + " " + GetString(status, "reason");
            rlog(std::string("resolve: ") + last_error_);
            continue;
        }

        if (PickAudio(root.get(), out, candidate.via))
        {
            out.user_agent = candidate.user_agent;
            rlog(std::string("resolve OK via ") + out.via);
            return out;
        }
        last_error_ = std::string(candidate.via) + ": no audio stream";
        rlog(std::string("resolve: ") + last_error_);
    }

    out.url.clear();
    return out;
}

bool InnertubeClient::DownloadFile(const std::string& url, const std::string& path)
{
    FILE* fp = fopen(path.c_str(), "wb");
    if (!fp)
        return false;

    CURL* curl = curl_easy_init();
    if (!curl)
    {
        fclose(fp);
        return false;
    }

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_USERAGENT, kUserAgent);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, FileWriteCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, fp);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 60L);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);

    const CURLcode rc = curl_easy_perform(curl);
    long http_code = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
    curl_easy_cleanup(curl);
    fclose(fp);

    if (rc != CURLE_OK || http_code != 200)
    {
        std::remove(path.c_str());
        return false;
    }
    return true;
}

} // namespace ssnx::yt
