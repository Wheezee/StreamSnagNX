#pragma once

#include <string>

namespace ssnx::yt
{

// Solves the YouTube n throttling parameter for googlevideo URLs.
// Extracts the player base.js, extracts the n-transform function,
// runs it inside embedded QuickJS, and returns the URL with the solved n.
// Returns the original URL untouched if no n parameter is present or on any error.
std::string SolveN(const std::string& url, const std::string& video_id);

void InvalidateSigCache();

} // namespace ssnx::yt
