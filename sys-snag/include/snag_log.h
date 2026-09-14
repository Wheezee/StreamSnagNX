#pragma once

// Minimal best-effort SD-card logger for sys-snag diagnostics.
// Appends tick-timestamped lines to sdmc:/switch/StreamSnagNX/sys-snag.log.
// Safe to call when SDMC is mounted (after __appInit); silently drops lines otherwise.
//
// WARNING: logging performs IPC (fs) on the calling thread's TLS. Never call
// this between parsing an incoming IPC request and finishing all reads of
// TLS-backed data (buffer descriptors, inline payloads) for that request.

#ifdef __cplusplus
extern "C" {
#endif

void SnagLog(const char* fmt, ...);

#ifdef __cplusplus
}
#endif
