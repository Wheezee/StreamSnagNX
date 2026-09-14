# StreamSnagNX — Offline YT Audio Grabber for Switch

> Created: 2026-09-12 | Status: Sprint 2 built, navigation fix pending hardware test
> Local: `E:\SwitchHomebrew\StreamSnagNX` | TitleID `05534E41474E5858` | v0.1.0

## What This Is
Native Switch homebrew music player. Search YouTube Music (no login), download
audio-only streams to SD, play offline. Zero auth, zero servers, zero accounts.
Library + playlists live in JSON on SD. Lyrics via LRCLIB (free, no key).

## Build
Prereqs: devkitPro + devkitA64 + Switch portlibs (curl, jansson) + CMake + Ninja
(MSYS2). Same toolchain as OpenNOW-Switch — see `E:\SwitchHomebrew\AGENTS.md`.
```bash
# devkitPro MSYS2 shell:
cmake -B build -G Ninja && cmake --build build --target StreamSnagNX.nro -j4
# output: build/StreamSnagNX.nro (~4.7MB)
```
Deploy: delete old copy, put NRO at `sdmc:/switch/StreamSnagNX.nro`, run via
**title takeover** (hold R + launch a game). Applet mode (Album) is untested.
SD data: `sdmc:/switch/StreamSnagNX/` (thumbs/, music/, library.json, boot.log).

## Layout
```
app/src/
  main.cpp            Borealis init, TabFrame + LB/RB switching, boot.log, console fallback
  search_tab.{hpp,cpp}  swkbd prompt, async InnerTube search, thumb cache, focusable rows
  yt/track.hpp        Track {title, artist, subtitle, video_id, thumb_url}
  yt/innertube.{hpp,cpp}  curl POST + jansson parse (search; player resolve = sprint 3)
resources/            icon.jpg (custom), font/, i18n/, img/, material/ (Borealis ROMFS set)
CMakeLists.txt        devkitA64 + Borealis (reused from OpenNOW extern/) + curl/jansson
```

## Hard Lessons (do not regress)
1. **switch_wrapper.c is mandatory.** Borealis's `lib/platforms/switch/switch_wrapper.c`
   provides `userAppInit()` → `romfsInit()` + services. Without it: `romfs:/` ENODEV,
   Borealis i18n throws at init. (Found via Sonnet handoff; OpenNOW does the same.)
2. **RomFS must be real.** ASET header must carry icon/nacp/romfs; verify with parser
   (64-bit fields! first parse used 32-bit and lied). Stale SD copies mimic RomFS
   bugs — always delete-then-copy, confirm byte size.
3. **Title takeover, not Album.** Applet RAM kills media apps (2168-0002 abort).
4. **Focus:** rows need `setFocusable(true)`; LB/RB tab switching (TabFrame sidebar
   d-pad can trap focus).
5. **Never rebuild lists under focus.** Thumb completion used to call ShowResults()
   (clearViews+rebuild) → use-after-free crash on focused row. Now: in-place
   `Image` updates via `thumb_views_` map + `search_gen_` staleness guard.
6. **Assign before render.** `ShowResults()` reads `last_tracks_` — it was called
   BEFORE the assignment (first search showed "N results, zero rows"). Order matters.
7. **visitorData is required for tokenless streams.** `sw.js_data` yields
   `visitorData` token to pass in `context.client` for anonymous player calls.
8. **No raw HTTP Range headers on googlevideo.** `googlevideo.com` 403s on `Range: bytes=0-`.
   Instead, range query parameters `&range=START-END&rn=N` in 1 MiB chunks must be appended
   to the URL on a single keep-alive connection (`switch-newpipe` pattern).
9. **FFmpeg custom/file protocols.** Local files passed to `avformat_open_input` require
   the `"file:"` prefix to prevent `AVERROR_PROTOCOL_NOT_FOUND`.

## References (patterns only — no GPL code lifted)
- `E:\SwitchHomebrew\Spotui\innertube/` — InnerTube client config: music.youtube.com
  host, `X-Goog-Api-Format-Version/Client-Name/Version` headers, client fallback chain
  (ANDROID_VR/iPad/Android), WEB_REMIX for search. Spotui is GPL-3.0: read, don't copy.
- `E:\SwitchHomebrew\switch-newpipe/src/common/throttling_decrypter.cpp` — `n`-param
  solver pattern: fetch base.js from embed page, regex-extract transform, run in
  embedded QuickJS, rewrite URL. Also VISIONOS HLS path. Our fallback if direct
  audio URLs get throttled.
- `E:\SwitchHomebrew\switch-newpipe/src/switch/switch_player.cpp` — `visitorData` bootstrap
  via `sw.js_data`, chunked ranged downloads (`&range=...&rn=...`), and format selection.
- `E:\SwitchHomebrew\OpenNOW-Switch` — Borealis app skeleton, swkbd prompt pattern
  (`game_detail_view.cpp`), curl/jansson usage, TopBarFrame chips, build system.
- UI guide: `E:\SwitchHomebrew\musichub-nro/mockup.html` (working prototype: search,
  library+playlists, now-playing+lyrics, settings). Prototype proxy
  (`musichub-nro/proxy.js`) exists ONLY for browser CORS — NRO uses curl direct.
- Lyrics: LRCLIB `GET /api/get` + `/api/search` (no key, synced LRC + plain).

## Plan (agile, one sprint ships only after hardware confirm)
- [x] Sprint 1: shell boots to tabs (TabFrame, fake rows, icon, boot.log)
- [x] Sprint 2: live InnerTube search (WEB_REMIX, rows + thumb cache)
- [x] Sprint 2.5: navigation (LB/RB, focusable rows) + crash fixes (in-place thumbs, row order)
- [x] Sprint 3: audio resolve (`VISIONOS` / `ANDROID_VR` / `IOS` with `visitorData`) + QuickJS `n`-solver + chunked ranged dl + FFmpeg/Audren playback
- [x] Sprint 4: LRCLIB synced lyrics + highlight/autoscroll
- [x] Sprint 5: downloads (worker, .m4a/.opus + .lrc sidecar) + library.json + playlists.json + sidenav
- [x] Sprint 6: settings that matter (quality cyclers, theme Blue/Green/Red/Purple, sleep timer)
- Later/maybe: up-next queue, LRC offset, seek/volume keys, Spotify metadata import (opt-in)

## Conventions
- C++20, Borealis views, curl+jansson for net, FFmpeg for audio (sprint 3+).
- Keep network JSON parsing defensive (unknown keys ignored, caps on results).
- No credentials/keys in repo. No repo-local account/token files.
- `E:\SwitchHomebrew\musichub-nro\` is the throwaway web prototype — do not ship it.
