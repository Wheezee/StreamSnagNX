#pragma once

#include "../yt/track.hpp"

#include <cstdint>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

namespace ssnx::library
{

struct LibraryTrack
{
    std::string video_id;
    std::string title;
    std::string artist;
    std::string subtitle;
    std::string thumb_url;
    std::string local_audio_path;
    std::string local_lrc_path;
    std::string local_thumb_path;
    std::string ext = "m4a";
    uint64_t added_at = 0;
};

struct Playlist
{
    std::string id;
    std::string name;
    std::vector<std::string> video_ids;
    uint64_t created_at = 0;
};

class LibraryStore
{
  public:
    static LibraryStore& Instance();

    void Init();
    void Reload();
    void Save();

    bool IsSaved(const std::string& video_id);
    std::vector<LibraryTrack> GetTracks();
    std::optional<LibraryTrack> GetTrack(const std::string& video_id);

    bool AddOrUpdateTrack(const LibraryTrack& track);
    bool RemoveTrack(const std::string& video_id);

    std::vector<Playlist> GetPlaylists();
    std::optional<Playlist> GetPlaylist(const std::string& id);
    std::string CreatePlaylist(const std::string& name);
    bool DeletePlaylist(const std::string& id);
    bool AddTrackToPlaylist(const std::string& playlist_id, const std::string& video_id);
    bool RemoveTrackFromPlaylist(const std::string& playlist_id, const std::string& video_id);

    static std::string StorageDir();
    static std::string MusicDir();
    static std::string ThumbsDir();
    static std::string AudioPath(const std::string& video_id, const std::string& ext);
    static std::string LrcPath(const std::string& video_id);
    static std::string ThumbPath(const std::string& video_id);

  private:
    LibraryStore() = default;

    void SaveLibraryLocked();
    void SavePlaylistsLocked();
    void LoadLibraryLocked();
    void LoadPlaylistsLocked();

    std::recursive_mutex mutex_;
    std::vector<LibraryTrack> tracks_;
    std::vector<Playlist> playlists_;
    bool initialized_ = false;
};

} // namespace ssnx::library
