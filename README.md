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
| `MouseScalingFix` | `1` | Fixes rounding of small mouse movements, especially noticeable when using scoped weapons. |
| `AdaptiveScreenCenter` | `1` | Calculates the screen center from the current resolution instead of using fixed values. |
| `ScaleCursorClipToResolution` | `1` | Uses the current resolution when confining the cursor instead of fixed 640x480 bounds. |
| `RestoreCursorClip` | `1` | Restores cursor confinement after Alt+Tab, focus, resolution, display, or window changes. Works independently of `RawMouseInput`. |

### `[Presentation]`

| Option | Default | Description |
| --- | :---: | --- |
| `Enabled` | `1` | Captures authoritative game-tick snapshots and maintains a separate render-only Presentation State. Requires `RawMouseInput=1`. |
| `CameraPresentation` | `1` | Allows Presentation State to replace only the final camera yaw/pitch, preserving the game's position, roll, bob, shake, and simulation state. |
| `RenderRateMouse` | `1` | Applies mouse movement received after the latest simulation tick on every rendered frame. It never writes predicted angles into gameplay state. |

The presentation camera learns the game's effective mouse-to-angle scale from authoritative ticks, so existing sensitivity and inversion behavior remain the source of truth. It rebases after focus/input transitions and leaves the legacy simulation cadence unchanged.

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
MouseScalingFix=1
AdaptiveScreenCenter=1
ScaleCursorClipToResolution=1
RestoreCursorClip=1

[Presentation]
Enabled=1
CameraPresentation=1
RenderRateMouse=1

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
