# BHD QoL dinput8 proxy

This repository contains a `dinput8.dll` proxy for *Delta Force: Black Hawk Down* (`dfbhd.exe`). Put the compiled DLL in the same directory as `dfbhd.exe`; Windows will load it before the system DirectInput 8 DLL, then the proxy forwards DirectInput calls to the real system DLL.

## Patch included

The DLL patches `SetupNVGViewPort()` at process attach time to increase the night-vision viewport from 512x256 to 2048x1024:

| Address | Original | Patched |
| --- | --- | --- |
| `0052A934` | `MOV EAX,0x200` | `MOV EAX,0x800` |
| `0052A94C` | `MOV [00E0498C],0x200` | `MOV [00E0498C],0x800` |
| `0052A956` | `MOV [00E04990],0x100` | `MOV [00E04990],0x400` |

The DLL also installs dynamic-resolution viewport hooks:

| Address | Original | Patched | Purpose |
| --- | --- | --- | --- |
| `004BC711` | `MOV DWORD PTR [009FB3D8],EBX` | `JMP 005E4879` | Compute additional resolution values in `LoadingGame()` |
| `004623D2` | `MOV DWORD PTR [EBP + -0x8],0x27F` | `JMP 005E4912` | Include dynamic resolutions in `InitializeInGameSystems()` ClipCursor setup |
| `004628D3` | `MOV DWORD PTR [EBP + -0x8],0x27F` | `JMP 005E492B` | Include dynamic resolutions in `ClipCursorToViewPort()` |

Each patch checks the expected bytes before writing, so unsupported executables are left unchanged.

## Raw mouse input

The optional Raw Input backend hooks the supported build's `PollMouseInput()` at
`005678F0`, registers the game window for `WM_INPUT` with
`RIDEV_NOLEGACY | RIDEV_CAPTUREMOUSE`, and feeds accumulated relative mouse
movement into the game's existing delta fields. `RIDEV_CAPTUREMOUSE` prevents a
mouse click from activating another window; it does not confine the physical
cursor to the game window.
Raw button and wheel transitions are sent directly through the game's mouse
dispatcher, preserving its bindings, sensitivity, inversion, and action pipeline.

The backend validates the original bytes before installing its detour and keeps a
trampoline to the original polling function as a fallback until Raw Input has been
registered successfully. It clears pending input on focus changes, preserves the
last valid game clip, and resumes only after the restored window is visible,
unminimized, foreground, focused, and confined again. Absolute-device motion is
ignored rather than treated as relative deltas.


## Runtime configuration

Copy `dinput8.ini` next to `dfbhd.exe` and the compiled `dinput8.dll` to enable or disable patch groups at runtime:

```ini
[PatchGroups]
DPIAware=1
NVGResolution=1
DynamicResolution=1
ClipCursorFix=1
RawMouseInput=1
RestoreCursorClip=1

[Logging]
Enabled=1
RawInputStatisticsIntervalMs=5000
```

Set a patch group to `1` to enable it or `0` to disable it. `RawMouseInput`
selects relative `WM_INPUT` mouse handling, while `RestoreCursorClip` independently
restores cursor confinement after focus, display, and window changes. Both are enabled
by default. They share one window subclass, so enabling both does not install competing
WndProc hooks. Either feature can be disabled without disabling the other. If the Raw
Input hook cannot validate the supported executable or complete registration, the DLL
retains its legacy mouse path while cursor restoration can continue independently. If
`dinput8.ini` is missing, the DLL uses the same defaults shown above.

`DPIAware=1` requests System DPI awareness before the real DirectInput entry point
is called and before the game creates its window. The DLL tries the modern context
API first, then the Windows 8.1 process API, and finally the Vista-era API, resolving
all functions dynamically for compatibility with older Windows versions. It does not
replace an awareness mode that is already configured (including Per-Monitor awareness),
and an unavailable API is non-fatal. An executable manifest remains preferable when
the game executable can be modified.

`Logging.Enabled=1` creates a new automatically named
`BHD_QoL_<date>_<time>_<pid>.log` beside `dfbhd.exe` for every session. Every line
is flushed immediately, so no DebugView installation is required and diagnostics
normally survive a game crash. `RawInputStatisticsIntervalMs` controls only the
frequency of aggregated Raw Input statistics and cursor-confinement snapshots
(valid range 1000-60000 ms, default 5000); it never delays mouse input.

The log records the effective configuration, executable validation, every patch
result, Raw Input registration and fallback, button and wheel transitions, focus
changes, and periodic movement statistics. Cursor snapshots compare the game
client rectangle, current `ClipCursor` rectangle, virtual desktop, physical cursor
position, per-monitor bounds, foreground/focus state, visibility, minimization,
and the game's internal resolution. A snapshot distinguishes
`confined_to_full_client`, `confined_inside_client`, `confined_to_other_rect`,
`not_confined`, and `clip_query_failed`. `outside_client=1` means the physical
cursor is outside the game client area, `can_escape=yes` means the current clip
permits that movement, and `on_other_monitor=1` confirms that the cursor is on a
different monitor from the game window.

Focus and cursor recovery are transition-based: repeated Windows activation,
focus, size, or display notifications do not reapply an already-valid clip or
restart an already-active Raw Input backend.

## Knowledge_Base policy

The `Knowledge_Base/` directory is a read-only reference area for LLM agents such as Codex. It may contain code, examples, or material extracted from other projects so that maintainers and agents can study that knowledge and use it as a basis for implementing changes in the main project tree.

Do not create, edit, move, rename, delete, format, or otherwise modify files under `Knowledge_Base/`. All implementation changes must be applied outside `Knowledge_Base/`, in the main repository files.

## Build

Cross-compile for 32-bit Windows with MinGW:

```sh
cmake -S . -B build -DCMAKE_TOOLCHAIN_FILE=mingw32-toolchain.cmake
cmake --build build
```

Alternatively, configure CMake with any i686 MinGW toolchain that produces a 32-bit Windows DLL named `dinput8.dll`.
