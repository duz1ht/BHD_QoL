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
| `DPIAware` | `1` | Prevents Windows DPI scaling from distorting window, monitor, and cursor coordinates. Recommended for borderless mode. |
| `RawMouseInput` | `1` | Uses Windows Raw Input for more reliable relative mouse input while preserving the game's sensitivity, inversion, and bindings. |
| `VisualMouseLateLatching` | `0` | Enables fail-closed capture/render sequence diagnostics. Visual correction is not applied until the authoritative producer boundary is proven at runtime. |
| `MouseScalingFix` | `1` | Fixes rounding of small mouse movements, especially noticeable when using scoped weapons. |
| `AdaptiveScreenCenter` | `1` | Calculates the screen center from the current resolution instead of using fixed values. |
| `ScaleCursorClipToResolution` | `1` | Uses the current resolution when confining the cursor instead of fixed 640x480 bounds. |
| `RestoreCursorClip` | `1` | Restores cursor confinement after Alt+Tab, focus, resolution, display, or window changes. Works independently of `RawMouseInput`. |

### `[Logging]`

| Option | Default | Description |
| --- | :---: | --- |
| `Enabled` | `0` | Creates a new `BHD_QoL_<date>_<time>_<pid>.log` beside `dfbhd.exe` for each session. Enable it to diagnose failures or features that were not applied. |
| `RawInputStatisticsIntervalMs` | `5000` | Sets the interval, in milliseconds, for Raw Input and cursor-confinement diagnostics in the log. Accepts values from `1000` to `60000` and does not affect mouse latency. |

Complete default configuration:

```ini
[PatchGroups]
BorderlessFullscreen=1
ForceDesktopResolution=1
UseCorrectAspectFOV=1
DPIAware=1
RawMouseInput=1
VisualMouseLateLatching=0
MouseScalingFix=1
AdaptiveScreenCenter=1
ScaleCursorClipToResolution=1
RestoreCursorClip=1

[Logging]
Enabled=0
RawInputStatisticsIntervalMs=5000
```

## Visual mouse latency diagnostics

The optional `VisualMouseLateLatching` rollout is separate from camera/FOV features and is disabled by default. At present, setting it to `1` records captured, pending, consumed, rebase, and bypass sequence diagnostics while deliberately leaving the native camera and viewmodel unchanged. See [`docs/input-pipeline.md`](docs/input-pipeline.md) for the proven input path and validation plan.

A future validated render-only correction can make the displayed camera and viewmodel follow render rate, but gameplay, firing, hit detection, and networking remain authoritative at approximately 62.5 Hz. A temporary displayed-aim/authoritative-direction difference is therefore possible. The feature reduces perceived visual latency; it does not increase the simulation tick, and every unproven prerequisite falls back to native behavior.

## Building

The project must be built as a 32-bit Windows DLL. With MinGW:

```sh
cmake -S . -B build -DCMAKE_TOOLCHAIN_FILE=mingw32-toolchain.cmake
cmake --build build
```

The resulting DLL must be named `dinput8.dll`.

## Compatibility

Before applying each modification, the DLL verifies the expected bytes in the executable. If the `dfbhd.exe` version is incompatible, the affected modification is not applied. Set `Logging.Enabled=1` to check the results.
