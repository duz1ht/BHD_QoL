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
| `HighFrequencyCamera` | `1` | Applies Raw Input movement that arrived after the latest game update to the local first-person presentation camera. Requires `RawMouseInput=1`. |
| `HighFrequencyViewmodel` | `1` | Makes the local first-person weapon and hands follow the presentation-camera yaw and pitch. Requires `HighFrequencyCamera=1`. |
| `MouseScalingFix` | `1` | Fixes rounding of small mouse movements, especially noticeable when using scoped weapons. |
| `AdaptiveScreenCenter` | `1` | Calculates the screen center from the current resolution instead of using fixed values. |
| `ScaleCursorClipToResolution` | `1` | Uses the current resolution when confining the cursor instead of fixed 640x480 bounds. |
| `RestoreCursorClip` | `1` | Restores cursor confinement after Alt+Tab, focus, resolution, display, or window changes. Works independently of `RawMouseInput`. |

Complete default configuration:

```ini
[PatchGroups]
BorderlessFullscreen=1
ForceDesktopResolution=1
UseCorrectAspectFOV=1
DPIAware=1
RawMouseInput=1
HighFrequencyCamera=1
HighFrequencyViewmodel=1
MouseScalingFix=1
AdaptiveScreenCenter=1
ScaleCursorClipToResolution=1
RestoreCursorClip=1

```

`RawMouseInput` receives mouse packets on a dedicated message thread so movement
can reach presentation frames between the game's fixed simulation updates. Each
`WM_INPUT` event is read directly and then `GetRawInputBuffer` drains any reports
that accumulated in the queue, which prevents high polling-rate mice from being
limited by one-message-at-a-time processing.
`HighFrequencyCamera` projects the game's native two-update mouse smoothing, then
uses the current sensitivity, vertical inversion, and scoped-view divisor. The
temporary visual angles are restored immediately after the camera is built; the
simulation consumes each movement once through its original path. The feature
currently applies to the local player in first person. Third-person, vehicle,
and spectator cameras retain their native behavior.
`HighFrequencyViewmodel` applies that presentation-only yaw and pitch difference to a
temporary copy of the first-person weapon pose while it is drawn. Weapon simulation,
animations, firing, and networking remain native.

## Building

The project must be built as a 32-bit Windows DLL. With MinGW:

```sh
cmake -S . -B build -DCMAKE_TOOLCHAIN_FILE=mingw32-toolchain.cmake
cmake --build build
```

The resulting DLL must be named `dinput8.dll`.

## Compatibility

Before applying each modification, the DLL verifies the expected bytes in the executable. If the `dfbhd.exe` version is incompatible, the affected modification is not applied.
