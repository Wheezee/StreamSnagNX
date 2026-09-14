#include "library_store.hpp"
#include "../util/log.hpp"

#ifdef __SWITCH__
#include <switch.h>
#include <sys/stat.h>
#endif

#include <jansson.h>

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <memory>
#include <unistd.h>

namespace ssnx::library
{
namespace
{

void LibLog(const std::string& line)
{
    ssnx::Log(line);
}

struct JsonDeleter
{
    void operator()(json_t* j) const
    {
        if (j)
            json_decref(j);
    }
};

using JsonPtr = std::unique_ptr<json_t, JsonDeleter>;

std::string GetString(json_t* obj, const char* key)
{
    if (!json_is_object(obj))
        return "";
    json_t* val = json_object_get(obj, key);
    if (!json_is_string(val))
        return "";
    const char* s = json_string_value(val);
    return s ? s : "";
}

uint64_t GetInt(json_t* obj, const char* key)
{
    if (!json_is_object(obj))
        return 0;
    json_t* val = json_object_get(obj, key);
    if (!json_is_integer(val))
        return 0;
    return static_cast<uint64_t>(json_integer_value(val));
}

void WriteJsonAtomic(const std::string& path, json_t* root)
{
    const std::string tmp_path = path + ".tmp";
    FILE* fp = fopen(tmp_path.c_str(), "wb");
    if (!fp)
    {
        fp = fopen(path.c_str(), "wb");
        if (!fp)
        {
            LibLog("libstore: fopen failed for " + path);
            return;
        }
        char* dump = json_dumps(root, JSON_INDENT(2));
        if (dump)
        {
            fputs(dump, fp);
            free(dump);
        }
        fflush(fp);
        fclose(fp);
#ifdef __SWITCH__
        fsdevCommitDevice("sdmc");
#endif
        return;
    }

    char* dump = json_dumps(root, JSON_INDENT(2));
    if (dump)
    {
        fputs(dump, fp);
        free(dump);
    }
    fflush(fp);
    fclose(fp);

    std::remove(path.c_str());
    if (std::rename(tmp_path.c_str(), path.c_str()) != 0)
    {
        LibLog("libstore: rename failed for " + path + ": " + std::string(strerror(errno)));
    }
#ifdef __SWITCH__
    fsdevCommitDevice("sdmc");
#endif
}

} // namespace

std::string LibraryStore::StorageDir()
{
#ifdef __SWITCH__
    return "sdmc:/switch/StreamSnagNX";
#else
    return ".";
#endif
}

std::string LibraryStore::MusicDir()
{
    return StorageDir() + "/music";
}

std::string LibraryStore::ThumbsDir()
{
    return StorageDir() + "/thumbs";
}

std::string LibraryStore::AudioPath(const std::string& video_id, const std::string& ext)
{
    return MusicDir() + "/" + video_id + "." + (ext.empty() ? "m4a" : ext);
}

std::string LibraryStore::LrcPath(const std::string& video_id)
{
    return MusicDir() + "/" + video_id + ".lrc";
}

std::string LibraryStore::ThumbPath(const std::string& video_id)
{
    return ThumbsDir() + "/" + video_id + ".jpg";
}

LibraryStore& LibraryStore::Instance()
{
    static LibraryStore store;
    return store;
}

void LibraryStore::Init()
{
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    if (initialized_)
        return;

#ifdef __SWITCH__
    mkdir("sdmc:/switch", 0777);
    mkdir(StorageDir().c_str(), 0777);
    mkdir(MusicDir().c_str(), 0777);
    mkdir(ThumbsDir().c_str(), 0777);
#endif

    LoadLibraryLocked();
    LoadPlaylistsLocked();
    initialized_ = true;
}

void LibraryStore::Reload()
{
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    LoadLibraryLocked();
    LoadPlaylistsLocked();
}

void LibraryStore::Save()
{
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    SaveLibraryLocked();
    SavePlaylistsLocked();
}

bool LibraryStore::IsSaved(const std::string& video_id)
{
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    for (const auto& track : tracks_)
    {
        if (track.video_id == video_id)
            return true;
    }
    return false;
}

std::vector<LibraryTrack> LibraryStore::GetTracks()
{
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    return tracks_;
}

std::optional<LibraryTrack> LibraryStore::GetTrack(const std::string& video_id)
{
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    for (const auto& track : tracks_)
    {
        if (track.video_id == video_id)
            return track;
    }
    return std::nullopt;
}

bool LibraryStore::AddOrUpdateTrack(const LibraryTrack& track)
{
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    auto it = std::find_if(tracks_.begin(), tracks_.end(),
                           [&](const LibraryTrack& t) { return t.video_id == track.video_id; });
    if (it != tracks_.end())
    {
        *it = track;
    }
    else
    {
        tracks_.insert(tracks_.begin(), track);
    }
    SaveLibraryLocked();
    return true;
}

bool LibraryStore::RemoveTrack(const std::string& video_id)
{
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    auto it = std::find_if(tracks_.begin(), tracks_.end(),
                           [&](const LibraryTrack& t) { return t.video_id == video_id; });
    if (it == tracks_.end())
        return false;

    // Delete local files
    if (!it->local_audio_path.empty())
        std::remove(it->local_audio_path.c_str());
    if (!it->local_lrc_path.empty())
        std::remove(it->local_lrc_path.c_str());

    tracks_.erase(it);
    SaveLibraryLocked();

    // Also remove from all playlists
    bool modified_pl = false;
    for (auto& pl : playlists_)
    {
        auto vit = std::remove(pl.video_ids.begin(), pl.video_ids.end(), video_id);
        if (vit != pl.video_ids.end())
        {
            pl.video_ids.erase(vit, pl.video_ids.end());
            modified_pl = true;
        }
    }
    if (modified_pl)
        SavePlaylistsLocked();

    return true;
}

std::vector<Playlist> LibraryStore::GetPlaylists()
{
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    return playlists_;
}

std::optional<Playlist> LibraryStore::GetPlaylist(const std::string& id)
{
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    for (const auto& pl : playlists_)
    {
        if (pl.id == id)
            return pl;
    }
    return std::nullopt;
}

std::string LibraryStore::CreatePlaylist(const std::string& name)
{
    if (name.empty())
        return "";

    std::lock_guard<std::recursive_mutex> lock(mutex_);
    const uint64_t now = static_cast<uint64_t>(
        std::chrono::system_clock::now().time_since_epoch().count());
    const std::string id = "pl_" + std::to_string(now);

    Playlist pl;
    pl.id = id;
    pl.name = name;
    pl.created_at = now;
    playlists_.push_back(std::move(pl));

    SavePlaylistsLocked();
    return id;
}

bool LibraryStore::DeletePlaylist(const std::string& id)
{
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    auto it = std::find_if(playlists_.begin(), playlists_.end(),
                           [&](const Playlist& pl) { return pl.id == id; });
    if (it == playlists_.end())
        return false;

    playlists_.erase(it);
    SavePlaylistsLocked();
    return true;
}

bool LibraryStore::AddTrackToPlaylist(const std::string& playlist_id, const std::string& video_id)
{
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    auto it = std::find_if(playlists_.begin(), playlists_.end(),
                           [&](const Playlist& pl) { return pl.id == playlist_id; });
    if (it == playlists_.end())
        return false;

    if (std::find(it->video_ids.begin(), it->video_ids.end(), video_id) == it->video_ids.end())
    {
        it->video_ids.push_back(video_id);
        SavePlaylistsLocked();
    }
    return true;
}

bool LibraryStore::RemoveTrackFromPlaylist(const std::string& playlist_id, const std::string& video_id)
{
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    auto it = std::find_if(playlists_.begin(), playlists_.end(),
                           [&](const Playlist& pl) { return pl.id == playlist_id; });
    if (it == playlists_.end())
        return false;

    auto vit = std::remove(it->video_ids.begin(), it->video_ids.end(), video_id);
    if (vit != it->video_ids.end())
    {
        it->video_ids.erase(vit, it->video_ids.end());
        SavePlaylistsLocked();
        return true;
    }
    return false;
}

void LibraryStore::LoadLibraryLocked()
{
    tracks_.clear();
    const std::string path = StorageDir() + "/library.json";
    const std::string tmp_path = path + ".tmp";

    FILE* probe = fopen(path.c_str(), "rb");
    if (!probe)
    {
        FILE* tmp_probe = fopen(tmp_path.c_str(), "rb");
        if (tmp_probe)
        {
            fclose(tmp_probe);
            std::rename(tmp_path.c_str(), path.c_str());
            probe = fopen(path.c_str(), "rb");
        }
    }
    if (!probe)
    {
        LibLog("libstore: library.json does not exist yet");
        return;
    }
    fclose(probe);

    json_error_t err {};
    json_t* raw = json_load_file(path.c_str(), 0, &err);
    if (!raw)
    {
        LibLog("libstore: library.json load error: " + std::string(err.text));
        return;
    }
    JsonPtr root(raw);
    if (!json_is_array(root.get()))
        return;

    size_t idx = 0;
    json_t* elem = nullptr;
    json_array_foreach(root.get(), idx, elem)
    {
        if (!json_is_object(elem))
            continue;
        LibraryTrack t;
        t.video_id = GetString(elem, "video_id");
        if (t.video_id.empty())
            continue;
        t.title = GetString(elem, "title");
        t.artist = GetString(elem, "artist");
        t.subtitle = GetString(elem, "subtitle");
        t.thumb_url = GetString(elem, "thumb_url");
        t.local_audio_path = GetString(elem, "local_audio_path");
        t.local_lrc_path = GetString(elem, "local_lrc_path");
        t.local_thumb_path = GetString(elem, "local_thumb_path");
        t.ext = GetString(elem, "ext");
        if (t.ext.empty())
            t.ext = "m4a";
        t.added_at = GetInt(elem, "added_at");
        tracks_.push_back(std::move(t));
    }
    LibLog("libstore: loaded " + std::to_string(tracks_.size()) + " tracks");
}

void LibraryStore::SaveLibraryLocked()
{
    const std::string path = StorageDir() + "/library.json";
    JsonPtr root(json_array());
    for (const auto& t : tracks_)
    {
        json_t* item = json_object();
        json_object_set_new(item, "video_id", json_string(t.video_id.c_str()));
        json_object_set_new(item, "title", json_string(t.title.c_str()));
        json_object_set_new(item, "artist", json_string(t.artist.c_str()));
        json_object_set_new(item, "subtitle", json_string(t.subtitle.c_str()));
        json_object_set_new(item, "thumb_url", json_string(t.thumb_url.c_str()));
        json_object_set_new(item, "local_audio_path", json_string(t.local_audio_path.c_str()));
        json_object_set_new(item, "local_lrc_path", json_string(t.local_lrc_path.c_str()));
        json_object_set_new(item, "local_thumb_path", json_string(t.local_thumb_path.c_str()));
        json_object_set_new(item, "ext", json_string(t.ext.c_str()));
        json_object_set_new(item, "added_at", json_integer(t.added_at));
        json_array_append_new(root.get(), item);
    }
    WriteJsonAtomic(path, root.get());
    LibLog("libstore: saved " + std::to_string(tracks_.size()) + " tracks to library.json");
}

void LibraryStore::LoadPlaylistsLocked()
{
    playlists_.clear();
    const std::string path = StorageDir() + "/playlists.json";
    const std::string tmp_path = path + ".tmp";

    FILE* probe = fopen(path.c_str(), "rb");
    if (!probe)
    {
        FILE* tmp_probe = fopen(tmp_path.c_str(), "rb");
        if (tmp_probe)
        {
            fclose(tmp_probe);
            std::rename(tmp_path.c_str(), path.c_str());
            probe = fopen(path.c_str(), "rb");
        }
    }
    if (!probe)
    {
        LibLog("libstore: playlists.json does not exist yet");
        return;
    }
    fclose(probe);

    json_error_t err {};
    json_t* raw = json_load_file(path.c_str(), 0, &err);
    if (!raw)
    {
        LibLog("libstore: playlists.json load error: " + std::string(err.text));
        return;
    }
    JsonPtr root(raw);
    if (!json_is_array(root.get()))
        return;

    size_t idx = 0;
    json_t* elem = nullptr;
    json_array_foreach(root.get(), idx, elem)
    {
        if (!json_is_object(elem))
            continue;
        Playlist pl;
        pl.id = GetString(elem, "id");
        pl.name = GetString(elem, "name");
        pl.created_at = GetInt(elem, "created_at");
        json_t* vids = json_object_get(elem, "video_ids");
        if (json_is_array(vids))
        {
            size_t vidx = 0;
            json_t* velem = nullptr;
            json_array_foreach(vids, vidx, velem)
            {
                if (json_is_string(velem))
                    pl.video_ids.push_back(json_string_value(velem));
            }
        }
        if (!pl.id.empty() && !pl.name.empty())
            playlists_.push_back(std::move(pl));
    }
    LibLog("libstore: loaded " + std::to_string(playlists_.size()) + " playlists");
}

void LibraryStore::SavePlaylistsLocked()
{
    const std::string path = StorageDir() + "/playlists.json";
    JsonPtr root(json_array());
    for (const auto& pl : playlists_)
    {
        json_t* item = json_object();
        json_object_set_new(item, "id", json_string(pl.id.c_str()));
        json_object_set_new(item, "name", json_string(pl.name.c_str()));
        json_object_set_new(item, "created_at", json_integer(pl.created_at));

        json_t* vids = json_array();
        for (const auto& vid : pl.video_ids)
            json_array_append_new(vids, json_string(vid.c_str()));
        json_object_set_new(item, "video_ids", vids);

        json_array_append_new(root.get(), item);
    }
    WriteJsonAtomic(path, root.get());
    LibLog("libstore: saved " + std::to_string(playlists_.size()) + " playlists to playlists.json");
}

} // namespace ssnx::library

