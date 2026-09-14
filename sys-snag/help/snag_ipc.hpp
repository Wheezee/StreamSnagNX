#pragma once
#include <cstdint>

#define SNAG_SERVICE_NAME "snag"

enum SnagCommandId {
    SnagCmd_GetStatus = 1,
    SnagCmd_Play      = 2,
    SnagCmd_Pause     = 3,
    SnagCmd_Stop      = 4,
    SnagCmd_PlayFile  = 5,
    SnagCmd_Seek      = 6,
    SnagCmd_SetVolume = 7,
    SnagCmd_Next      = 8,
    SnagCmd_Prev      = 9,
    SnagCmd_PlayIndex = 10,
};

struct SnagStatus {
    uint32_t is_playing;     // 1 if playing, 0 if paused/stopped
    uint32_t is_initialized; // 1 if audio engine & hwopus are initialized
    uint32_t position_ms;    // Current playback timestamp in milliseconds
    uint32_t duration_ms;    // Track duration in milliseconds
    float volume;            // 0.0f - 1.0f
    uint32_t track_index;    // Current track index in queue (0-based)
    uint32_t track_count;    // Total tracks in queue
    char current_path[256];  // Absolute path to audio file on SD
    char current_title[64];  // Track title
    char current_artist[64]; // Track artist
};

struct SnagPlayFileRequest {
    char path[256];
    char title[64];
    char artist[64];
};
