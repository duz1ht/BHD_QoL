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
Raw button and wheel transitions are sent immediately from the window's
`WM_INPUT` handler through the game's mouse dispatcher, so clicks are delivered
even in menus and modal interfaces that do not call `PollMouseInput()`. This
preserves the game's bindings, sensitivity, inversion, and action pipeline.

The backend validates the original bytes before installing its detour and keeps a
trampoline to the original polling function as a fallback until Raw Input has been
registered successfully. It clears pending input on focus changes, preserves the
last valid game clip, and resumes only after the restored window is visible,
unminimized, foreground, focused, and confined again. Absolute-device motion is
ignored rather than treated as relative deltas.

During startup, legacy polling remains active until Raw Input receives valid game-window
focus for the first time. The window thread performs a guarded initial focus recovery so
borderless initialization cannot leave the menu cursor waiting for an Alt+Tab cycle.


## Runtime configuration

Copy `dinput8.ini` next to `dfbhd.exe` and the compiled `dinput8.dll` to enable or disable patch groups at runtime:

```ini
[PatchGroups]
BorderlessFullscreen=0
ForceDesktopResolution=0
UseCorrectAspectFOV=1
DPIAware=1
RawMouseInput=1
MouseScalingFix=1
AdaptiveScreenCenter=1
ClipCursorFix=1
RestoreCursorClip=1
NVGResolution=1

[Logging]
Enabled=0
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

The shared window hook also verifies the real foreground process independently of
Alt+Tab messages. When the Start menu or another application takes foreground, it
releases cursor confinement, suspends game input, unregisters mouse capture, and
temporarily balances the game's hidden-cursor state so the Windows cursor remains
visible. Capture, gamma, and the compensating visibility changes are restored only
after the game has regained both foreground and input focus.

`MouseScalingFix=1` replaces the scoped mouse fixed-point conversion with signed
fractional accumulation for independent X and Y axes. This prevents tiny movement in
one direction from being discarded while the opposite direction moves a whole unit.
The hook is enabled by default, resets accumulated fractions when the scope scale
changes or focus is lost, and leaves unscoped and whole-unit scaling on the game's
original conversion path. Set it to `0` to disable the hook.

`UseCorrectAspectFOV=1` preserves the vertical framing of the original 80-degree
horizontal FOV at 4:3 and expands the horizontal FOV for wider displayed aspect
ratios. Borderless mode uses the physical output size, so correction remains
accurate when a 4:3 internal resolution is stretched to widescreen. The override
applies only when the current, target, and temporary camera FOV values are all
exactly 80 degrees, preserving scopes, zoom, special cameras, transitions, and
map-controlled FOV values. It defaults to `1`; set it to `0` to disable the
client-only camera hook.

`DPIAware=1` requests System DPI awareness before the real DirectInput entry point
is called and before the game creates its window. The DLL tries the modern context
API first, then the Windows 8.1 process API, and finally the Vista-era API, resolving
all functions dynamically for compatibility with older Windows versions. It does not
replace an awareness mode that is already configured (including Per-Monitor awareness),
and an unavailable API is non-fatal. An executable manifest remains preferable when
the game executable can be modified.

`BorderlessFullscreen=1` forces the game's D3D8 windowed path, removes the caption,
border, and resize frame, and covers the complete rectangle (not the work area) of the
monitor nearest its initial window. `ForceDesktopResolution=0` preserves the resolution
selected in game and stretches it to that window; setting it to `1` forces the internal
render resolution to the monitor dimensions. Both options default to `0`, and
`ForceDesktopResolution` is ignored when borderless fullscreen is disabled. Keep
`DPIAware=1` enabled so Windows does not virtualize monitor coordinates.

Borderless fullscreen automatically applies the exact gamma ramp generated by the game
to the monitor containing the game window, because D3D8 ignores device gamma ramps in
windowed mode. This behavior is always enabled with borderless fullscreen and is not a
user-configurable patch group. The desktop ramp is captured before use, restored whenever
the game loses focus or closes, and reapplied when focus returns. Since this uses the
display gamma ramp, the correction temporarily affects everything shown on that monitor
while the game is focused. Some display drivers or HDR configurations may reject
gamma-ramp changes; such failures are logged and do not prevent the game from running.

| BorderlessFullscreen | ForceDesktopResolution | Result |
| --- | --- | --- |
| `0` | either | Borderless mode is disabled and the resolution option is ignored. |
| `1` | `0` | The selected in-game resolution is stretched to the monitor. |
| `1` | `1` | The internal render resolution is changed to the monitor dimensions. |

When logging is enabled, `BorderlessFullscreen active=1` confirms the popup style,
window and client rectangles, and windowed-render state. The log reports the render
resolution and output size separately and indicates whether scaling is active. When
desktop resolution is forced, the feature also patches the supported executable's
resolution setup before device creation; an executable signature mismatch disables only
this feature. Until the game publishes a positive internal resolution, diagnostics show
`render_resolution=unknown` rather than treating the startup `0x0` value as final.

Logging is disabled by default. Set `Logging.Enabled=1` to create a new automatically named
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
