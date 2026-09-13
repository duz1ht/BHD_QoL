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
| `BorderlessFullscreen` | `0` | Runs the game in a borderless window that covers the entire monitor. |
| `ForceDesktopResolution` | `0` | With `BorderlessFullscreen=1`, uses the monitor resolution as the internal render resolution. At `0`, the resolution selected in the game is stretched to fill the screen. Has no effect when borderless mode is disabled. |
| `UseCorrectAspectFOV` | `1` | Corrects the field of view for aspect ratios other than 4:3 without changing zoom, scopes, or special cameras. |
| `DPIAware` | `1` | Prevents Windows DPI scaling from distorting window, monitor, and cursor coordinates. Recommended for borderless mode. |
| `RawMouseInput` | `1` | Uses Windows Raw Input for more reliable relative mouse input while preserving the game's sensitivity, inversion, and bindings. |
| `MouseScalingFix` | `1` | Fixes rounding of small mouse movements, especially noticeable when using scoped weapons. |
| `AdaptiveScreenCenter` | `1` | Calculates the screen center from the current resolution instead of using fixed values. |
| `ClipCursorFix` | `1` | Uses the current resolution when confining the cursor instead of fixed 640x480 bounds. |
| `RestoreCursorClip` | `1` | Restores cursor confinement after Alt+Tab, focus, resolution, display, or window changes. Works independently of `RawMouseInput`. |
| `NVGResolution` | `1` | Increases the night-vision render resolution from 512x256 to 2048x1024. |

### `[Logging]`

| Option | Default | Description |
| --- | :---: | --- |
| `Enabled` | `0` | Creates a new `BHD_QoL_<date>_<time>_<pid>.log` beside `dfbhd.exe` for each session. Enable it to diagnose failures or features that were not applied. |
| `RawInputStatisticsIntervalMs` | `5000` | Sets the interval, in milliseconds, for Raw Input and cursor-confinement diagnostics in the log. Accepts values from `1000` to `60000` and does not affect mouse latency. |

Complete default configuration:

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

## Building

The project must be built as a 32-bit Windows DLL. With MinGW:

```sh
cmake -S . -B build -DCMAKE_TOOLCHAIN_FILE=mingw32-toolchain.cmake
cmake --build build
```

The resulting DLL must be named `dinput8.dll`.

## Compatibility

Before applying each modification, the DLL verifies the expected bytes in the executable. If the `dfbhd.exe` version is incompatible, the affected modification is not applied. Set `Logging.Enabled=1` to check the results.

## For LLM agents

`Knowledge_Base/` is a read-only reference area. Do not modify any file or directory inside it.
