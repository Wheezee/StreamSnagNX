# StreamSnagNX Background Music Sysmodule (`sys-snag`) — Problem Summary & Handover

> **Context**: Nintendo Switch Homebrew (Horizon OS / Atmosphere CFW)  
> **Repository**: `StreamSnagNX/sys-snag`  
> **Folder**: `E:\SwitchHomebrew\StreamSnagNX\sys-snag\help` contains all relevant source files and reference files from `sys-tune`.

---

## 1. Executive Goal
The main foreground app (`StreamSnagNX.nro`) downloads YouTube Music tracks to SD storage at:
`sdmc:/switch/StreamSnagNX/music/<video_id>.[opus|m4a]`
along with synced lyrics `.lrc` sidecars and `library.json`.

Because launching any official Switch game suspends/closes foreground NROs, we need a background Atmosphere sysmodule:
* **Target**: `sys-snag` (Atmosphere TitleID / Program ID: `4200000000534E47`)
* **Installed at**: `sdmc:/atmosphere/contents/4200000000534E47/exefs.nsp` with `flags/boot2.flag`
* **Purpose**: Demux and decode `.opus` / `.webm` (Matroska) / `.ogg` files in background using Nintendo Switch hardware Opus decoder (`hwopus`) and output audio via `audout` (or `audren`), allowing zero-overhead background music playback while games are running.
* **Control**: Companion CLI tool `snag-ctl.nro` (`tools/snag-ctl/`) or Tesla overlay `StreamSnag.ovl` connecting to the sysmodule via named IPC port (`"snag"`).

---

## 2. The Failures & Error Codes Encountered

### Failure 1: "Failed to play: <track>.opus" in `snag-ctl`
* `snag-ctl.nro` launches and lists music files from `sdmc:/switch/StreamSnagNX/music/`.
* When pressing A or X to play, it returns `Failed to play: <filename>.opus`.
* IPC connection succeeds, but either `demuxer_.Open()` fails, `hwopus_.Initialize()` fails, or the playback pipeline aborts.

### Failure 2: Atmosphere Boot Crash `2001-0132 (0x10801)` on Program `0100000000000023`
* Happened when `pool_partition` was changed from `2` to `0` (or `1`) in `config.json`.
* **Explanation**: Pool partition 0 is the `Application` RAM pool. Sysmodules launching at `boot2` cannot acquire or steal this partition without starving Horizon OS qlaunch (`0100000000000023`), causing an unbootable CFW crash.
* **Resolution**: Reverted `pool_partition: 2` (System Non-Secure, exactly what `sys-tune` uses).

### Failure 3: Crash `2168-0002 (0x4a8)`
* When running `snag-ctl.nro` on the Switch, or when `snag-ctl` attempts an operation, Atmosphere throws error code `2168-0002 (0x4a8)`.
* In Horizon OS / Atmosphere, `0x4a8` corresponds to an intentional abort / assertion trigger (`diagAbortWithResult`, `R_ABORT_UNLESS`, stack overflow, or uncaught exception).
* **Question**: Is `snag-ctl.nro` crashing, or is `sys-snag` crashing during IPC handling or startup? (Check `sdmc:/atmosphere/crash_reports/`).

---

## 3. Architecture & File Guide in this `help/` Folder

| File in `help/` | Description |
|---|---|
| `README_HELP.md` | This handover guide with full timeline, crash details, and hypotheses. |
| `config.json` | Atmosphere NPDM config (TitleID `4200000000534E47`, `pool_partition: 2`, thread priority, capabilities). |
| `main.cpp` | Entry point of `sys-snag`. Sets heap, `__appInit`, `__appExit`, and `main()`. |
| `player_daemon.cpp` / `.hpp` | Core daemon singleton (`PlayerDaemon`). Spawns worker thread, manages playback loop. |
| `audren_engine.cpp` / `.hpp` | Audio engine wrapping `audout` with static page-aligned BSS buffers and `audoutWaitPlayFinish()`. |
| `hwopus_decoder.cpp` / `.hpp` | Wrapper around libnx `hwopusDecoderInitialize()` and `hwopusDecodeInterleaved()`. |
| `webm_demuxer.cpp` | Demuxer supporting WebM (EBML / Matroska) and Ogg containers to extract raw Opus packets. |
| `snag_server.cpp` | IPC dispatcher listening on named port `"snag"`. |
| `snag_ipc.hpp` | IPC command IDs and payload structs. |
| `snag_client.hpp` | IPC client library used by `snag-ctl`. |
| `snag_ctl_main.cpp` | The text-mode controller NRO (`snag-ctl.nro`) providing UI to play/pause/skip and list files. |
| **Reference: `sys-tune`** | |
| `sys-tune_main.cpp` | Proven working entrypoint of `sys-tune`. |
| `sys-tune_music_player.cpp` | Proven working player implementation of `sys-tune` (`audoutInitialize`, `AudioMemoryPool`, `audoutWaitPlayFinish`). |

---

## 4. History of What Was Attempted & Changed

1. **Queue scan file filters**:
   `PlayerDaemon::ScanMusicDir()` only checked `.opus`, `.webm`, `.ogg`. Added `.m4a`.
2. **IPC Struct Buffer Sizes**:
   `SnagPlayFileRequest` and `SnagStatus` had `path[88]` which risked truncating longer SD paths like `sdmc:/switch/StreamSnagNX/music/<id>.opus`. Expanded `path` to 256 bytes in `snag_ipc.hpp`.
3. **Metadata Lookup in Controller**:
   Updated `snag_ctl_main.cpp` to read `sdmc:/switch/StreamSnagNX/library.json` to extract proper title & artist by video_id rather than displaying raw hashes.
4. **NPDM Pool Partition Trials**:
   * Tried `pool_partition: 0` -> Brick/crash `2001-0132 (0x10801)`.
   * Tried `pool_partition: 1` -> Crash.
   * Restored `pool_partition: 2` (Standard system non-secure, matching `sys-tune`).
5. **Memory & Heap**:
   Tested `INNER_HEAP_SIZE` values: 250KB, 512KB, 2MB, 6MB. Current is 500KB with static BSS audio buffers.
6. **Audren vs. Audout**:
   * Original code attempted to use `audren` (Audio Renderer via `audrenInitialize` & `audrvCreate`). Audren requires a huge work buffer (~2-3 MB) and usually fails under `pool_partition: 2`.
   * Rewrote `audren_engine` to use `audout` (Audio Output via `audoutInitialize`, `audoutAppendAudioOutBuffer`, `audoutWaitPlayFinish`), exactly mirroring `sys-tune`'s `music_player.cpp`.
7. **`audoutInitialize()` Timing**:
   * Calling `audoutInitialize()` inside `__appInit()` triggered aborts because audio services are not yet available at early `boot2`.
   * Moved `audoutInitialize()` into `main()`.

---

## 5. Prime Suspects & Open Diagnostic Questions

### A. Check the Atmosphere Crash Report (`sdmc:/atmosphere/crash_reports/`)
When error `2168-0002 (0x4a8)` occurs, Atmosphere generates a crash report file.
* **Which process actually died?**
  * If the crashing TitleID is `4200000000534E47`: The sysmodule itself aborted.
  * If the crashing TitleID is the homebrew launcher / application: `snag-ctl.nro` crashed.
* **What is the Program counter (PC) or function symbol?**
* **What is the Result / Abort code register?** (e.g., in X0 or X1).

### B. Is `audoutInitialize()` in `main()` failing and aborting?
In `sys-snag/source/main.cpp`:
```cpp
Result rc = audoutInitialize();
if (R_FAILED(rc))
    diagAbortWithResult(rc);
```
In `sys-tune`:
`sys-tune` initializes `audWrapperInitialize()` in `__appInit()` (which accesses `auda` / `audouta` to manage master process volume), and calls `audoutInitialize()` inside `TuneThreadFunc` or `tune::impl::Initialize()` from `main()`.
If `audoutInitialize()` fails at startup in `main()`, `diagAbortWithResult(rc)` will immediately crash the sysmodule with `2168-0002`!
* **Hypothesis**: Should `audoutInitialize()` be initialized lazily only when playing a track, or is there a required service dependency?

### C. Can `hwopus` be used inside a background sysmodule?
`PlayerDaemon::Initialize()` calls:
```cpp
hwopus_.Initialize(48000, 2);
```
Which executes:
```cpp
hwopusDecoderInitialize(&decoder_, sample_rate, channels);
```
* Does the `hwopus` service allow sessions from a sysmodule running in `pool_partition: 2`?
* Does `hwopus` require specific service permissions or SM access that a standard sysmodule doesn't have?
* Does `hwopusDecoderInitialize` fail with an unhandled result code?

### D. File Descriptor & SDMC Access
* `__appInit` runs `fsdevMountSdmc()`. If the SD card is unmounted or filesystem sessions are exhausted (`__nx_fs_num_sessions = 1`), `demuxer_.Open()` will fail.
* Does `PlayerDaemon` run before SDMC is mounted?

### E. IPC Port Conflict or CMIF Buffer Failure
In `snag_server.cpp` / `ipc_server.c`:
* Is `"snag"` port already in use or failing to register?
* In `ipcServerInit`: `svcManageNamedPort(&server->handles[0], "snag", max_sessions)`.
* Does `snag_client.hpp` correctly communicate with this CMIF layout?

---

## 6. How to Build & Deploy

From root of project (MSYS2 / devkitA64 environment):
```bash
# Sysmodule:
cd sys-snag
make clean && make -j4
# Output: sys-snag.nsp

# Controller CLI:
cd tools/snag-ctl
make clean && make -j4
# Output: snag-ctl.nro
```

Deployment paths on SD card:
```
sdmc:/atmosphere/contents/4200000000534E47/exefs.nsp  <- sys-snag.nsp
sdmc:/atmosphere/contents/4200000000534E47/flags/boot2.flag
sdmc:/switch/snag-ctl.nro                             <- snag-ctl.nro
```
Restart Switch / reboot Atmosphere after updating `exefs.nsp`.
