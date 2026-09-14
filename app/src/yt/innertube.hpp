#pragma once

#include "track.hpp"

#include <string>
#include <vector>

namespace ssnx::yt
{

// Minimal InnerTube client (music.youtube.com host, no auth).
struct ResolvedAudio
{
    std::string url;
    std::string via; // e.g. "IOS/140"
    std::string ext; // "m4a" or "opus"
    // UA of the client that minted the URL. googlevideo checks consistency;
    // native clients send only this (no Origin/Referer).
    std::string user_agent;
};

class InnertubeClient
{
  public:
    // Returns up to 20 tracks. Empty on any failure; check last_error().
    std::vector<Track> Search(const std::string& query);
    // Resolves a playable audio URL (iPad client first, ANDROID fallback,
    // itag 140 m4a then 251 opus). Empty url on failure.
    ResolvedAudio ResolveAudio(const std::string& video_id);
    const std::string& last_error() const { return last_error_; }

    // Downloads a URL to a local file. True on HTTP 200 with bytes.
    static bool DownloadFile(const std::string& url, const std::string& path);

  private:
    static std::string Post(const std::string& endpoint, const std::string& body,
                            const std::string& client_id, const std::string& client_version);
    std::string last_error_;
};

} // namespace ssnx::yt
