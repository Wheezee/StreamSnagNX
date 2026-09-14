# StreamSnag Overlay — State Machine (no-deadlock contract)

> Two binaries, Status Monitor architecture. Player keeps the 448px sidebar
> layer; float is its own 1280x64 strip binary with a lean 1920x96 layer
> parked at screen bottom (proportional layer math, copied from Status
> Monitor's libtesla fork — our 2023 Tesla derived layer size from aspect
> ratio, which forced a full 1920x1080 layer and OOM'd (0x559)).
> Rule: the chord is ONE-WAY (float -> player). It is NEVER a toggle.
> Cross-process jumps use relaunch; in-stack steps use goBack. No traps.

```
                        ┌──────────────┐
                        │     GAME     │  music may play, nothing visible
                        └──────────────┘
                          ▲          │
              B-exit /    │          │  Ultrahand menu -> StreamSnag
              chord-close │          ▼
              (loader)    │   ┌──────────────┐
                          │   │    PLAYER    │  bar, volume, Browse, Lyrics
                          │   │  B -> exit to Ultrahand menu (stack empties)
                          │   └──────────────┘
                          │     ▲  │  A on "Floating Lyrics" (changeTo)
                          │     │  │  menu chrome hides, pill only
                          │     │  ▼
                          │   ┌──────────────┐
                          └───┤    FLOAT     │  lyrics pill only, game owns
                              │  ALL buttons (B swallowed)            │
                              │  chord (hold ZL+ZR+Minus) -> PLAYER   │
                              │  tap pill -> PLAYER                   │
                              └──────────────┘
```

## Entry edge (auto-float REMOVED 2026-09-14)

The overlay always opens at PLAYER. Auto-float-on-open was tried and
removed: it left chord-exits with an empty stack, dumping out to HOME
instead of the player. Strict 3-state loop below — no exceptions.

## Exit inventory (audit after every change)

| From   | Input                | To      | Mechanism (no loader APIs) |
|--------|----------------------|---------|----------------------------|
| FLOAT  | chord ZL+ZR+Minus    | PLAYER  | `goBack()` (or menu if initial) |
| FLOAT  | tap pill (touch)     | PLAYER  | `goBack()` / `changeTo`    |
| FLOAT  | B / any game button  | nowhere | swallowed; game handles it |
| PLAYER | B                    | menu    | stack empties (Tesla default) |
| PLAYER | A on Floating Lyrics | FLOAT   | `changeTo`                 |
| menu   | chord                | GAME    | loader default toggle      |

If a future change adds a screen, it MUST add its row here first.
