#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include <fstream>
#include <algorithm>

struct LyricEntry {
    uint32_t timestamp_ms = 0;
    std::string text;
};

class LrcParser {
public:
    static std::vector<LyricEntry> LoadFromFile(const std::string& path)
    {
        std::vector<LyricEntry> entries;
        std::ifstream file(path);
        if (!file.is_open())
            return entries;

        std::string line;
        while (std::getline(file, line))
        {
            if (line.size() < 4)
                continue;

            // Look for [mm:ss.xx]
            size_t tag_start = line.find('[');
            size_t tag_end = line.find(']');
            if (tag_start == std::string::npos || tag_end == std::string::npos || tag_end <= tag_start)
                continue;

            std::string time_str = line.substr(tag_start + 1, tag_end - tag_start - 1);
            size_t colon = time_str.find(':');
            if (colon == std::string::npos)
                continue;

            char* end_p1 = nullptr;
            unsigned long min = std::strtoul(time_str.c_str(), &end_p1, 10);
            if (end_p1 == time_str.c_str() || *end_p1 != ':')
                continue;

            char* end_p2 = nullptr;
            double sec = std::strtod(end_p1 + 1, &end_p2);
            uint32_t ms = static_cast<uint32_t>(min * 60000 + static_cast<uint32_t>(sec * 1000.0));

            std::string text = line.substr(tag_end + 1);
            // Trim leading whitespace
            size_t first_non_space = text.find_first_not_of(" \t\r\n");
            if (first_non_space != std::string::npos)
                text = text.substr(first_non_space);
            else
                text.clear();

            // Trim trailing \r or \n
            while (!text.empty() && (text.back() == '\r' || text.back() == '\n'))
                text.pop_back();

            entries.push_back({ ms, text });
        }

        std::sort(entries.begin(), entries.end(), [](const LyricEntry& a, const LyricEntry& b) {
            return a.timestamp_ms < b.timestamp_ms;
        });

        return entries;
    }

    static std::string GetActiveLine(const std::vector<LyricEntry>& entries, uint32_t current_ms)
    {
        if (entries.empty())
            return "";

        if (current_ms < entries.front().timestamp_ms)
            return "...";

        // Binary search for the latest entry with timestamp <= current_ms
        int left = 0;
        int right = static_cast<int>(entries.size()) - 1;
        int best = 0;

        while (left <= right)
        {
            int mid = left + (right - left) / 2;
            if (entries[mid].timestamp_ms <= current_ms)
            {
                best = mid;
                left = mid + 1;
            }
            else
            {
                right = mid - 1;
            }
        }

        return entries[best].text;
    }
};
