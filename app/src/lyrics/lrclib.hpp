#pragma once

#include <string>
#include <vector>

namespace ssnx::lyrics
{

struct LyricLine
{
    double timestamp = 0.0; // In seconds from start of track
    std::string text;
};

struct LyricsResult
{
    bool is_synced = false;
    bool is_plain = false;
    bool is_instrumental = false;
    std::vector<LyricLine> synced;
    std::vector<std::string> plain;
    std::string error;
};

struct LyricCandidate
{
    int id = 0;
    std::string track_name;
    std::string artist_name;
    std::string album_name;
    double duration = 0.0;
    bool is_synced = false;
    bool is_plain = false;
    bool is_instrumental = false;
    std::string synced_lyrics;
    std::string plain_lyrics;
};

class LrclibClient
{
  public:
    static LyricsResult FetchLyrics(const std::string& title, const std::string& artist,
                                    double duration = 0.0);

    static std::vector<LyricCandidate> SearchCandidates(const std::string& query);

    static std::vector<LyricLine> ParseLRC(const std::string& lrc_content);
    static std::string CleanTitle(const std::string& raw_title);
    static std::string CleanArtist(const std::string& raw_artist);
    static std::string ExtractCoreTitle(const std::string& raw_title);
};

} // namespace ssnx::lyrics

