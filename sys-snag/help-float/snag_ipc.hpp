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

// Scalar core — the ONLY inline IPC payload (28 bytes).
// Rule (sys-tune pattern): inline data stays tiny; all strings travel in a
// HipcMapAlias buffer. Oversized inline payloads poison the client session.
struct SnagStatusCore {
    uint32_t is_playing;     // 1 if playing, 0 if paused/stopped
    uint32_t is_initialized; // 1 if audio engine & hwopus are initialized
    uint32_t position_ms;    // Current playback timestamp in milliseconds
    uint32_t duration_ms;    // Track duration in milliseconds
    float volume;            // 0.0f - 1.0f
    uint32_t track_index;    // Current track index in queue (0-based)
    uint32_t track_count;    // Total tracks in queue
};
static_assert(sizeof(SnagStatusCore) == 28, "SnagStatusCore must stay 28 bytes");

// String blob layout inside one SNAG_STRBUF_SIZE HipcMapAlias buffer:
//   path   @ offset 0   (SNAG_PATH_LEN bytes incl NUL)
//   title  @ offset 256 (SNAG_TITLE_LEN bytes incl NUL)
//   artist @ offset 320 (SNAG_ARTIST_LEN bytes incl NUL)
#define SNAG_STRBUF_SIZE 384
#define SNAG_PATH_OFF    0
#define SNAG_PATH_LEN    256
#define SNAG_TITLE_OFF   256
#define SNAG_TITLE_LEN   64
#define SNAG_ARTIST_OFF  320
#define SNAG_ARTIST_LEN  64

// Client-side assembled view. Never transferred as a whole: the core goes
// inline, the string fields are filled via the alias buffer (same offsets).
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
