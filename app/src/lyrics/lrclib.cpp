#include "lrclib.hpp"

#include <curl/curl.h>
#include <jansson.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <memory>
#include <regex>
#include <sstream>

namespace ssnx::lyrics
{
namespace
{

constexpr const char* kLrclibHost = "https://lrclib.net/api/";
constexpr const char* kUserAgent = "StreamSnagNX/0.1.0 (Nintendo Switch; LRCLIB client)";

using JsonPtr = std::unique_ptr<json_t, decltype(&json_decref)>;

size_t WriteCallback(char* ptr, size_t size, size_t nmemb, void* userdata)
{
    auto* out = static_cast<std::string*>(userdata);
    out->append(ptr, size * nmemb);
    return size * nmemb;
}

std::string UrlEncode(CURL* curl, const std::string& str)
{
    if (str.empty())
        return "";
    char* encoded = curl_easy_escape(curl, str.c_str(), static_cast<int>(str.length()));
    if (!encoded)
        return "";
    std::string result = encoded;
    curl_free(encoded);
    return result;
}

std::string HttpGet(const std::string& url)
{
    std::string response;
    CURL* curl = curl_easy_init();
    if (!curl)
        return "";

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_USERAGENT, kUserAgent);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_MAXREDIRS, 5L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 4L);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 3L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);

    const CURLcode rc = curl_easy_perform(curl);
    long http_code = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
    curl_easy_cleanup(curl);

    if (rc != CURLE_OK || (http_code != 200 && http_code != 404))
        return "";

    return response;
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

bool GetBool(json_t* object, const char* key)
{
    if (!json_is_object(object))
        return false;
    json_t* value = json_object_get(object, key);
    return json_is_true(value);
}

void ParseItem(json_t* item, LyricsResult& out)
{
    if (!json_is_object(item))
        return;

    out.is_instrumental = GetBool(item, "instrumental");

    const std::string synced = GetString(item, "syncedLyrics");
    if (!synced.empty())
    {
        out.synced = LrclibClient::ParseLRC(synced);
        out.is_synced = !out.synced.empty();
    }

    const std::string plain = GetString(item, "plainLyrics");
    if (!plain.empty())
    {
        out.is_plain = true;
        std::stringstream ss(plain);
        std::string line;
        while (std::getline(ss, line))
        {
            if (!line.empty() && line.back() == '\r')
                line.pop_back();
            out.plain.push_back(line);
        }
    }
}

bool TryGet(CURL* curl, const std::string& track_name, const std::string& artist_name, LyricsResult& out)
{
    if (track_name.empty())
        return false;
    std::string get_url = std::string(kLrclibHost) + "get?track_name=" + UrlEncode(curl, track_name);
    if (!artist_name.empty())
        get_url += "&artist_name=" + UrlEncode(curl, artist_name);

    std::string response = HttpGet(get_url);
    if (response.empty())
        return false;

    json_error_t error {};
    JsonPtr root(json_loads(response.c_str(), 0, &error), &json_decref);
    if (!root || !json_is_object(root.get()) || json_object_get(root.get(), "error"))
        return false;

    LyricsResult res;
    ParseItem(root.get(), res);
    if (res.is_synced || res.is_plain || res.is_instrumental)
    {
        out = std::move(res);
        return true;
    }
    return false;
}

bool TrySearch(CURL* curl, const std::string& query, LyricsResult& out)
{
    if (query.empty())
        return false;
    std::string search_url = std::string(kLrclibHost) + "search?q=" + UrlEncode(curl, query);
    std::string response = HttpGet(search_url);
    if (response.empty())
        return false;

    json_error_t error {};
    JsonPtr root(json_loads(response.c_str(), 0, &error), &json_decref);
    if (!root || !json_is_array(root.get()) || json_array_size(root.get()) == 0)
        return false;

    // Prioritize synced lyrics, then plain lyrics, then top suggestion
    json_t* best_item = nullptr;
    size_t idx = 0;
    json_t* elem = nullptr;
    json_array_foreach(root.get(), idx, elem)
    {
        if (!json_is_object(elem))
            continue;
        if (!GetString(elem, "syncedLyrics").empty())
        {
            best_item = elem;
            break;
        }
        if (!best_item && !GetString(elem, "plainLyrics").empty())
            best_item = elem;
    }
    if (!best_item)
        best_item = json_array_get(root.get(), 0);

    LyricsResult res;
    ParseItem(best_item, res);
    if (res.is_synced || res.is_plain || res.is_instrumental)
    {
        out = std::move(res);
        return true;
    }
    return false;
}

} // namespace

std::string LrclibClient::ExtractCoreTitle(const std::string& raw_title)
{
    // 1. Standard quotes "..."
    size_t q1 = raw_title.find('"');
    if (q1 != std::string::npos)
    {
        size_t q2 = raw_title.find('"', q1 + 1);
        if (q2 != std::string::npos && q2 > q1 + 1)
            return raw_title.substr(q1 + 1, q2 - q1 - 1);
    }
    // 2. Japanese corner brackets 「...」
    const std::string jp1_open = "\xe3\x80\x8c"; // 「
    const std::string jp1_close = "\xe3\x80\x8d"; // 」
    size_t j1 = raw_title.find(jp1_open);
    if (j1 != std::string::npos)
    {
        size_t j2 = raw_title.find(jp1_close, j1 + jp1_open.size());
        if (j2 != std::string::npos)
            return raw_title.substr(j1 + jp1_open.size(), j2 - (j1 + jp1_open.size()));
    }
    // 3. Japanese double brackets 『...』
    const std::string jp2_open = "\xe3\x80\x8e"; // 『
    const std::string jp2_close = "\xe3\x80\x8f"; // 』
    size_t k1 = raw_title.find(jp2_open);
    if (k1 != std::string::npos)
    {
        size_t k2 = raw_title.find(jp2_close, k1 + jp2_open.size());
        if (k2 != std::string::npos)
            return raw_title.substr(k1 + jp2_open.size(), k2 - (k1 + jp2_open.size()));
    }
    // 4. Curly double quotes “...”
    const std::string c_open = "\xe2\x80\x9c"; // “
    const std::string c_close = "\xe2\x80\x9d"; // ”
    size_t c1 = raw_title.find(c_open);
    if (c1 != std::string::npos)
    {
        size_t c2 = raw_title.find(c_close, c1 + c_open.size());
        if (c2 != std::string::npos)
            return raw_title.substr(c1 + c_open.size(), c2 - (c1 + c_open.size()));
    }
    // 5. Single quotes '...' (if at least 3 chars long)
    size_t s1 = raw_title.find('\'');
    if (s1 != std::string::npos)
    {
        size_t s2 = raw_title.find('\'', s1 + 1);
        if (s2 != std::string::npos && s2 > s1 + 3)
            return raw_title.substr(s1 + 1, s2 - s1 - 1);
    }
    return "";
}

std::string LrclibClient::CleanTitle(const std::string& raw_title)
{
    std::string s = raw_title;
    // Strip common YouTube fluff
    static const std::vector<std::regex> kNoisePatterns = {
        std::regex(R"(\s*[\(\[]\s*official\s*(?:music)?\s*video\s*[\)\]])", std::regex::icase),
        std::regex(R"(\s*[\(\[]\s*official\s*audio\s*[\)\]])", std::regex::icase),
        std::regex(R"(\s*[\(\[]\s*lyric\s*video\s*[\)\]])", std::regex::icase),
        std::regex(R"(\s*[\(\[]\s*lyrics\s*[\)\]])", std::regex::icase),
        std::regex(R"(\s*[\(\[]\s*visualizer\s*[\)\]])", std::regex::icase),
        std::regex(R"(\s*[\(\[]\s*hd\s*[\)\]])", std::regex::icase),
        std::regex(R"(\s*[\(\[]\s*4k\s*[\)\]])", std::regex::icase),
        std::regex(R"(\s*[\(\[]\s*remaster(?:ed)?\s*[\)\]])", std::regex::icase),
        std::regex(R"(\s*[\(\[]\s*audio\s*[\)\]])", std::regex::icase),
        std::regex(R"(\s*[\(\[]\s*video\s*[\)\]])", std::regex::icase),
        std::regex(R"(\s*[\(\[]\s*(?:feat|ft)\.?\s+[^\)\]]+[\)\]])", std::regex::icase),
        std::regex(R"(\s+(?:feat|ft)\.?\s+.*$)", std::regex::icase),
    };

    for (const auto& pat : kNoisePatterns)
        s = std::regex_replace(s, pat, "");

    // If title is "Artist - Title", extract the Title part
    const size_t dash = s.find(" - ");
    if (dash != std::string::npos && dash + 3 < s.size())
        s = s.substr(dash + 3);

    // Trim leading/trailing whitespace
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.front())))
        s.erase(s.begin());
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back())))
        s.pop_back();

    return s;
}

std::string LrclibClient::CleanArtist(const std::string& raw_artist)
{
    std::string s = raw_artist;
    const size_t sep = s.find_first_of("·•");
    if (sep != std::string::npos)
        s = s.substr(0, sep);

    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.front())))
        s.erase(s.begin());
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back())))
        s.pop_back();

    return s;
}

std::vector<LyricLine> LrclibClient::ParseLRC(const std::string& lrc_content)
{
    std::vector<LyricLine> lines;
    std::stringstream ss(lrc_content);
    std::string raw_line;

    // Pattern for [mm:ss.xx] or [mm:ss.xxx] or [mm:ss]
    static const std::regex kTimestampPattern(R"(\[(\d{1,2}):(\d{2})(?:\.(\d{1,3}))?\](.*))");

    while (std::getline(ss, raw_line))
    {
        if (!raw_line.empty() && raw_line.back() == '\r')
            raw_line.pop_back();

        std::smatch match;
        if (std::regex_match(raw_line, match, kTimestampPattern))
        {
            const int minutes = std::stoi(match[1].str());
            const int seconds = std::stoi(match[2].str());
            double frac = 0.0;
            if (match[3].matched)
            {
                const std::string frac_str = match[3].str();
                if (frac_str.size() == 1)
                    frac = std::stod(frac_str) / 10.0;
                else if (frac_str.size() == 2)
                    frac = std::stod(frac_str) / 100.0;
                else
                    frac = std::stod(frac_str) / 1000.0;
            }

            LyricLine item;
            item.timestamp = static_cast<double>(minutes * 60 + seconds) + frac;
            item.text = match[4].str();
            // Trim leading spaces from text
            while (!item.text.empty() && std::isspace(static_cast<unsigned char>(item.text.front())))
                item.text.erase(item.text.begin());

            if (item.text.empty())
                item.text = "...";

            lines.push_back(std::move(item));
        }
    }

    std::sort(lines.begin(), lines.end(), [](const LyricLine& a, const LyricLine& b) {
        return a.timestamp < b.timestamp;
    });

    return lines;
}

LyricsResult LrclibClient::FetchLyrics(const std::string& title, const std::string& artist,
                                      double duration)
{
    LyricsResult result;
    const std::string clean_title = CleanTitle(title);
    const std::string clean_artist = CleanArtist(artist);
    const std::string core_title = CleanTitle(ExtractCoreTitle(title));

    if (clean_title.empty() && core_title.empty())
    {
        result.error = "Empty title";
        return result;
    }

    CURL* curl = curl_easy_init();
    if (!curl)
    {
        result.error = "Curl init failed";
        return result;
    }

    // Tier 1: Exact match with clean title & artist (/api/get)
    if (!clean_title.empty())
    {
        if (TryGet(curl, clean_title, clean_artist, result))
        {
            curl_easy_cleanup(curl);
            return result;
        }
    }

    // Tier 2: Extracted core/quoted title (e.g. Billy MV "Billy Mode" -> "Billy Mode")
    if (!core_title.empty() && core_title != clean_title)
    {
        if (TryGet(curl, core_title, clean_artist, result))
        {
            curl_easy_cleanup(curl);
            return result;
        }
        if (TrySearch(curl, core_title + (clean_artist.empty() ? "" : (" " + clean_artist)), result))
        {
            curl_easy_cleanup(curl);
            return result;
        }
        if (TrySearch(curl, core_title, result))
        {
            curl_easy_cleanup(curl);
            return result;
        }
    }

    // Tier 3: Suggestion search fallback, prioritizing synced then plain
    if (!clean_title.empty())
    {
        const std::string query = clean_title + (clean_artist.empty() ? "" : (" " + clean_artist));
        if (TrySearch(curl, query, result))
        {
            curl_easy_cleanup(curl);
            return result;
        }

        if (!clean_artist.empty())
        {
            if (TrySearch(curl, clean_title, result))
            {
                curl_easy_cleanup(curl);
                return result;
            }
        }
    }

    curl_easy_cleanup(curl);
    result.error = "No lyrics found";
    return result;
}

std::vector<LyricCandidate> LrclibClient::SearchCandidates(const std::string& query)
{
    std::vector<LyricCandidate> results;
    if (query.empty())
        return results;

    CURL* curl = curl_easy_init();
    if (!curl)
        return results;

    std::string search_url = std::string(kLrclibHost) + "search?q=" + UrlEncode(curl, query);
    std::string response = HttpGet(search_url);
    curl_easy_cleanup(curl);

    if (response.empty())
        return results;

    json_error_t error {};
    JsonPtr root(json_loads(response.c_str(), 0, &error), &json_decref);
    if (!root || !json_is_array(root.get()))
        return results;

    size_t idx = 0;
    json_t* elem = nullptr;
    json_array_foreach(root.get(), idx, elem)
    {
        if (!json_is_object(elem))
            continue;

        LyricCandidate c;
        json_t* id_val = json_object_get(elem, "id");
        if (json_is_integer(id_val))
            c.id = static_cast<int>(json_integer_value(id_val));
        c.track_name = GetString(elem, "trackName");
        c.artist_name = GetString(elem, "artistName");
        c.album_name = GetString(elem, "albumName");
        c.is_instrumental = GetBool(elem, "instrumental");
        c.synced_lyrics = GetString(elem, "syncedLyrics");
        c.plain_lyrics = GetString(elem, "plainLyrics");
        c.is_synced = !c.synced_lyrics.empty();
        c.is_plain = !c.plain_lyrics.empty();

        json_t* dur = json_object_get(elem, "duration");
        if (json_is_number(dur))
            c.duration = json_number_value(dur);

        if (c.is_synced || c.is_plain || c.is_instrumental)
            results.push_back(std::move(c));

        if (results.size() >= 10)
            break;
    }

    return results;
}

} // namespace ssnx::lyrics

