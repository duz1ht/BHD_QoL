# BHD QoL

A `dinput8.dll` proxy with quality-of-life improvements for **Delta Force: Black Hawk Down** (`dfbhd.exe`). The DLL loads the original Windows DirectInput library and applies only the options enabled in `dinput8.ini`.

## Installation

1. Copy `dinput8.dll` to the same directory as `dfbhd.exe`.
2. Copy `dinput8.ini` to that directory if you want to change the settings.
3. Launch the game normally.

Example:

```text
Game directory/
├── dfbhd.exe
├── dinput8.dll
└── dinput8.ini
```

The INI file is optional. If it is missing, the DLL uses the default values shown below. Use `1` to enable an option and `0` to disable it.

## `dinput8.ini` options

### `[PatchGroups]`

| Option | Default | Description |
| --- | :---: | --- |
| `BorderlessFullscreen` | `1` | Runs the game in a borderless window that covers the entire monitor. |
| `ForceDesktopResolution` | `1` | With `BorderlessFullscreen=1`, uses the monitor resolution as the internal render resolution. At `0`, the resolution selected in the game is stretched to fill the screen. Has no effect when borderless mode is disabled. |
| `UseCorrectAspectFOV` | `1` | Corrects the field of view for internal render resolutions wider than 4:3 without changing zoom, scopes, or special cameras. |
| `HighFrequencyVisualCamera` | `0` | Synchronizes render-only interpolation for the first-person camera, primary viewmodel, and guarded player/vehicle actor paths. Unknown render paths fall back to native transforms. It does not alter simulation/input ticks and adds up to one authoritative update (about 16 ms) of visual latency. |
| `DPIAware` | `1` | Prevents Windows DPI scaling from distorting window, monitor, and cursor coordinates. Recommended for borderless mode. |
| `RawMouseInput` | `1` | Uses Windows Raw Input for more reliable relative mouse input while preserving the game's sensitivity, inversion, and bindings. |
| `MouseScalingFix` | `1` | Preserves fractional movement for every fractional sensitivity scale, preventing small mouse movements from being rounded away in normal aim and scopes. |
| `RenderFrameLimit` | `0` | Optional render-only cap from 30 to 1000 FPS. Zero preserves original behavior. This does not increase the authoritative mouse/camera update frequency. |
| `AdaptiveScreenCenter` | `1` | Calculates the screen center from the current resolution instead of using fixed values. |
| `ScaleCursorClipToResolution` | `1` | Uses the current resolution when confining the cursor instead of fixed 640x480 bounds. |
| `RestoreCursorClip` | `1` | Restores cursor confinement after Alt+Tab, focus, resolution, display, or window changes. Works independently of `RawMouseInput`. |

### `[Logging]`

| Option | Default | Description |
| --- | :---: | --- |
| `Enabled` | `0` | Creates a new `BHD_QoL_<date>_<time>_<pid>.log` beside `dfbhd.exe` for each session. Enable it to diagnose failures or features that were not applied. |
| `FramePacingDiagnostics` | `0` | Hooks Direct3D 8 `CreateDevice`, `Reset`, and `Present` to report frame time, requested presentation interval/VSync, and input-poll-to-present delay. Requires logging and is diagnostic-only. |
| `RawInputStatisticsIntervalMs` | `5000` | Sets the interval, in milliseconds, for Raw Input, input-latency, scaling, and cursor-confinement diagnostics in the log. Accepts values from `1000` to `60000` and does not affect mouse latency. |

Complete default configuration:

```ini
[PatchGroups]
BorderlessFullscreen=1
ForceDesktopResolution=1
UseCorrectAspectFOV=1
HighFrequencyVisualCamera=0
DPIAware=1
RawMouseInput=1
MouseScalingFix=1
RenderFrameLimit=0
AdaptiveScreenCenter=1
ScaleCursorClipToResolution=1
RestoreCursorClip=1

[Logging]
Enabled=0
FramePacingDiagnostics=0
RawInputStatisticsIntervalMs=5000
```

## Building

The project must be built as a 32-bit Windows DLL. With MinGW:

```sh
cmake -S . -B build -DCMAKE_TOOLCHAIN_FILE=mingw32-toolchain.cmake
cmake --build build
```

The resulting DLL must be named `dinput8.dll`.
The frame-pacing diagnostics use a local declaration of the small Direct3D 8
ABI surface they require, so the legacy DirectX SDK and `d3d8.h` are not build
dependencies.

## Mouse diagnostics

Set `Logging.Enabled=1` to emit aggregated `RawInput.Latency` and
`MouseScaling.Stats` records. They show how long raw reports wait for the game
poll, input-consumption pacing, reports grouped per poll, empty polls,
fractional scaling calls, scale transitions, and input/output totals without
performing log I/O for each mouse report. See
[`docs/input-pipeline.md`](docs/input-pipeline.md) for the verified executable
path and field descriptions.

Enable `FramePacingDiagnostics=1` together with `Enabled=1` to also emit
`FramePacing.Config` and `FramePacing.Stats`. The former records the Direct3D 8
presentation parameters requested at device creation/reset. The latter reports
the configured and actual sample-window duration, average/maximum frame time,
stalls, estimated FPS, time blocked inside `Present`, failed presents, and delay
from the latest input poll to the following `Present`. It also reports input
polls and Presents per poll to expose camera/update cadence independently from
rendering. This option observes timing only;
it does not force VSync or change the game's presentation parameters.

Raw Input movement received before the first gameplay poll is intentionally
discarded so menu/loading movement cannot produce a delayed camera jump. Mouse
buttons, wheel events, and the virtual menu cursor remain active during that
period. The first poll also verifies cursor confinement immediately.

`RenderFrameLimit` can prevent the unlocked gameplay renderer from issuing
hundreds of redundant Presents per second. Its high-resolution deadline is
reset after long stalls and Direct3D device resets. When frame diagnostics are
also enabled, `frame_limit`, `limit_wait_avg_us`, `limit_wait_max_us`, and
`limit_misses` describe its behavior. The limiter affects rendering only and
does not pretend to raise the game's approximately 62 Hz authoritative camera
tick.

`HighFrequencyVisualCamera` captures the game's native fixed-step phase and
keeps the main first-person camera, primary viewmodel, and supported moving
actors on one render-only timeline. Position is interpolated linearly and the
three 32-bit angle fields follow the shortest modular arc. Camera and viewmodel
corrections are applied to stack-local data; actor corrections are applied to
bounded DLL-owned copies of final model palettes. Authoritative camera, actor,
aim, weapon, collision, AI, and network state remain untouched.

The synchronized path is deliberately fail-closed. It currently covers known
OITEM player/organic-actor and MITEM vehicle paths. Unknown producers,
projectiles, temporary objects, auxiliary pools, special camera modes, invalid
matrix counts, and submissions without a proven actor context retain native
transforms. Teleports and unexpected authoritative changes rebase instead of
being smoothed. The interpolation trails the latest authoritative state by up
to one update; leave it disabled if minimum input latency is more important
than visual smoothness. With Raw Input statistics and logging enabled,
`VisualInterpolation.Stats` reports guarded corrections and native fallbacks.
See the
[`high-frequency visual camera implementation guide`](docs/high-frequency-visual-camera-implementation-guide.md)
for the reusable architecture, target-specific hook map, safety rules, and
recommended porting sequence.

## Compatibility

Before applying each modification, the DLL verifies the expected bytes in the executable. If the `dfbhd.exe` version is incompatible, the affected modification is not applied. Set `Logging.Enabled=1` to check the results.
