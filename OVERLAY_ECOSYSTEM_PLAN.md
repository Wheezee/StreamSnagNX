# StreamSnagNX Background Music & In-Game Lyric Overlay Ecosystem

## Overview
StreamSnagNX currently downloads YouTube Music audio streams as native Opus (`.opus`) or AAC (`.m4a`) files alongside synced lyrics (`.lrc`) from LRCLIB. However, when the user launches a Nintendo Switch game, the foreground NRO is suspended/closed by Horizon OS. Existing background players like `sys-tune` only support MP3, FLAC, and WAV—meaning they cannot play StreamSnagNX's native YouTube downloads.

This plan details the architecture and implementation of a two-part companion ecosystem:
1. **`sys-snag` (Atmosphere Sysmodule / `exefs.nsp`)**: An ultra-lightweight background daemon leveraging the Nintendo Switch's built-in **`hwopus`** hardware decoder to play native `.opus` audio in the background during gameplay with 0% CPU impact.
2. **`StreamSnag.ovl` (Tesla Overlay / `.ovl`)**: A `libtesla` in-game companion menu allowing playback control, library browsing, and a **Floating In-Game Synced Lyrics HUD ("Micro Mode")** that renders synchronized karaoke lyrics over running games.

---

## Architecture Diagram

```
┌─────────────────────────────────────────────────────────────┐
│ 1. StreamSnagNX (Foreground NRO)                            │
│    • Downloads .opus audio + .lrc sidecars to SD card        │
│    • Saves library.json & playlists                         │
│    • Location: sdmc:/switch/StreamSnagNX/music/             │
└──────────────────────────────┬──────────────────────────────┘
                               │
                Saved Files on SD Card
                               │
                               ▼
┌─────────────────────────────────────────────────────────────┐
│ 2. sys-snag Daemon (Sysmodule - exefs.nsp)                  │
│    • Path: sdmc:/atmosphere/contents/4200000000534E47/      │
│    • Hardware Opus decoding via libnx hwopus service        │
│    • Audio output via audren (Audio Renderer)               │
│    • 0% CPU impact on running games                         │
│    • Named IPC service / local socket ("snag:tune")         │
│      - GetCurrentTrack() -> Path & Metadata                 │
│      - GetPositionMs()   -> u32 playback timestamp          │
│      - Play(), Pause(), Next(), Previous(), Seek()          │
└──────────────────────────────▲──────────────────────────────┘
                               │ IPC / Socket Protocol
                               ▼
┌─────────────────────────────────────────────────────────────┐
│ 3. StreamSnag Overlay (Tesla Overlay - StreamSnag.ovl)       │
│    • Path: sdmc:/switch/.overlays/StreamSnag.ovl            │
│    • Full Mode (Tesla Menu):                                │
│      - Track control (Play/Pause/Skip/Seek/Volume)          │
│      - Library / Playlist picker                            │
│      - Floating Lyrics Toggle (ON / OFF)                    │
│    • Micro Mode (During In-Game Gameplay):                  │
│      - Semi-transparent HUD at top or bottom of screen      │
│      - Real-time synced lyric matching via .lrc             │
│      - Full controller input passthrough to game            │
└─────────────────────────────────────────────────────────────┘
```

---

## Proposed Implementation Details

### Component 1: Sysmodule (`sys-snag`)
Path: `sys-snag/`

#### 1. `sys-snag/config.json`
- Atmosphere NPDM configuration defining sysmodule TitleID `4200000000534E47`, memory pool limits (heap size ~2-3 MB), and service access (`audren`, `hwopus`, `fs`).

#### 2. `sys-snag/source/main.cpp`
- Sysmodule entry point:
  - Initializes `sm`, `setsys`, `fsdev`, `audren`, and `hwopus`.
  - Spawns the audio playback thread and the IPC listener thread.
  - Registers clean exit hooks.

#### 3. `sys-snag/source/audio/hwopus_decoder.hpp/.cpp`
- Stream parser for Ogg Opus container (`.opus` files).
- Feeds raw Opus packets into `libnx`'s `hwopusDecodeInterleaved()`.
- Delivers decoded 48 kHz stereo PCM samples to the audio buffer queue with zero software CPU overhead.

#### 4. `sys-snag/source/audio/audren_engine.hpp/.cpp`
- Wraps `audren` / `audrv` (Nintendo Switch Audio Renderer).
- Handles voice allocation, volume ramping, audio mixing, and sleep/resume events.

#### 5. `sys-snag/source/ipc/snag_ipc.hpp/.cpp`
- Custom IPC interface or lightweight socket server:
  - `CMD_GET_STATUS`: returns playing/paused, current track path, position in ms, duration in ms.
  - `CMD_PLAY`, `CMD_PAUSE`, `CMD_NEXT`, `CMD_PREV`, `CMD_SEEK`, `CMD_SET_VOLUME`.
  - `CMD_PLAY_TRACK`: immediately load and play a specified file from `sdmc:/switch/StreamSnagNX/music/`.

#### 6. `sys-snag/Makefile`
- devkitA64 Makefile using `switch_rules` targeting `exefs.nsp` output via `elf2nso`, `npdmtool`, and `build_pfs0`.

---

### Component 2: Tesla Overlay (`StreamSnag.ovl`)
Path: `overlay/`

#### 1. `overlay/src/main.cpp`
- Overlay initialization using `tsl::loop<SnagOverlay>()`.
- Initializes connection to `sys-snag` IPC service upon overlay invocation.

#### 2. `overlay/src/gui/main_gui.hpp/.cpp`
- **Main View (Tesla Menu)**:
  - Header with current song title, artist, and playback status.
  - Interactive playback controls: Play/Pause, Next Track, Previous Track.
  - Track seek bar and volume slider.
  - Playlist / Song selector (browses `/switch/StreamSnagNX/music/` or `library.json`).
  - Toggle list item: `Floating Lyrics HUD` (Off, Top, Bottom).

#### 3. `overlay/src/gui/lyrics_hud_gui.hpp/.cpp`
- **Micro Mode (Floating Karaoke HUD)**:
  - Implements `tsl::Gui` with `tsl::elm::OverlayFrame` or custom NanoVG rendering.
  - Sets input passthrough so the Switch game receives all button/joystick inputs.
  - Draws a sleek, rounded semi-transparent dark pill box (alpha ~0.7).
  - Loads `.lrc` file matching the current playing track from SD.
  - Checks current timestamp from `sys-snag` every frame.
  - Displays the active lyric line, smoothly fading/scrolling when transitioning to the next timestamp.

#### 4. `overlay/src/lrc/lrc_parser.hpp/.cpp`
- Ultra-lightweight parser (< 100 LOC) for `.lrc` files:
  - Parses `[mm:ss.xx] Lyric text` lines into an array of `{ uint32_t timestamp_ms, std::string text }`.
  - Binary search lookup for $O(\log N)$ active line resolution based on current playback ms.

#### 5. `overlay/CMakeLists.txt`
- CMake build configured with `-fPIE -pie`, linking `libtesla` and `libnx`, building `StreamSnag.ovl`.

---

## Verification & Deployment Plan

### Build Verification
1. **Sysmodule Build**:
   ```bash
   cd sys-snag && make -j4
   # Output: sys-snag.nsp / exefs.nsp
   ```
2. **Overlay Build**:
   ```bash
   cd overlay && cmake -B build -G Ninja && cmake --build build -j4
   # Output: build/StreamSnag.ovl
   ```

### Hardware Verification on Nintendo Switch
1. **Deployment**:
   - Copy `exefs.nsp` to `sdmc:/atmosphere/contents/4200000000534E47/exefs.nsp`.
   - Ensure `flags/boot2.flag` exists in that directory so Atmosphere auto-boots the sysmodule.
   - Copy `StreamSnag.ovl` to `sdmc:/switch/.overlays/StreamSnag.ovl`.
   - Reboot Switch into Atmosphere.
2. **Background Audio Test**:
   - Launch any game (e.g. *Mario Kart 8 Deluxe*).
   - Press `L + D-Pad Down + RStick` to open Tesla menu.
   - Select `StreamSnag`, pick a `.opus` track downloaded by StreamSnagNX, hit Play.
   - Close Tesla menu. Confirm music plays smoothly during active gameplay with zero lag or frame drops.
3. **Floating Lyrics HUD Test**:
   - Open overlay, toggle `Floating Lyrics: ON`.
   - Return to game.
   - Verify game controls respond normally (input passthrough) and lyrics scroll in sync with audio.

