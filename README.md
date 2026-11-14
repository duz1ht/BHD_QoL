# BHD QoL

A `winmm.dll` proxy with quality-of-life improvements for **Delta Force: Black Hawk Down** (`dfbhd.exe`). The DLL forwards Windows multimedia calls to the original 32-bit system WinMM library and applies only the options enabled in `bhd_player_config.ini`.

## Installation

1. Copy the **32-bit** `winmm.dll` to the same directory as `dfbhd.exe`.
2. Copy `BHD Player Config.exe` and `bhd_player_config.ini` to the same directory.
3. Open `BHD Player Config.exe`, adjust the options and click **Save**.
4. Launch the game normally. Restart it after changing settings.

Example:

```text
Game directory/
├── dfbhd.exe
├── winmm.dll
├── BHD Player Config.exe
└── bhd_player_config.ini
```

The configurator always reads and writes `bhd_player_config.ini` beside its own executable. The DLL reads that same filename from the game directory. Keep the DLL, configurator and INI together. The INI file is optional; if it is missing, the DLL uses the default values shown below. The configurator can create it by clicking **Save**. You can also edit it manually, using `1` to enable an option and `0` to disable it.

The application includes **Settings**, **Crosshair**, **HUD** and **Fonts** tabs. Settings contains the options currently supported by the DLL. The other tabs reserve the interface for their respective features as they are implemented.

When upgrading from the old BHD QoL proxy, remove **this project's old**
`dinput8.dll` before installing `winmm.dll`, so the patches are not applied twice.
Copy your previous settings from `dinput8.ini` to `bhd_player_config.ini`. The new proxy does
not read `dinput8.ini`; another mod can use that filename independently. A
`dinput8.dll` supplied by another mod can coexist with this proxy, although mods
that patch the same game code still require compatibility testing. Only one
`winmm.dll` proxy can occupy the game directory.

## `bhd_player_config.ini` options

### `[PatchGroups]`

| Option | Default | Description |
| --- | :---: | --- |
| `BorderlessFullscreen` | `1` | Runs the game in a borderless window that covers the entire monitor. |
| `ForceDesktopResolution` | `1` | With `BorderlessFullscreen=1`, uses the monitor resolution as the internal render resolution. At `0`, the resolution selected in the game is stretched to fill the screen. Has no effect when borderless mode is disabled. |
| `UseCorrectAspectFOV` | `1` | Corrects the field of view for internal render resolutions wider than 4:3 without changing zoom, scopes, or special cameras. |
| `DPIAware` | `1` | Prevents Windows DPI scaling from distorting window, monitor, and cursor coordinates. Recommended for borderless mode. |
| `AllowHighResolutionFonts` | `1` | Detects the atlas dimensions of each loaded FNT0 file independently, allowing stock and higher-resolution fonts to coexist. Does not create or replace font assets. |
| `RawMouseInput` | `1` | Uses Windows Raw Input for more reliable relative mouse input while preserving the game's sensitivity, inversion, and bindings. |
| `HighFrequencyCamera` | `1` | Applies Raw Input movement that arrived after the latest game update to the local first-person presentation camera. Requires `RawMouseInput=1`. |
| `HighFrequencyCameraPosition` | `1` | Interpolates on-foot first-person camera XYZ to smooth static scenery while walking/strafing. Independent of Raw Input and mouse rotation. Adds up to one movement update of visual position lag. |
| `HighFrequencyViewmodel` | `1` | Makes the local first-person weapon and hands follow the presentation camera, with interpolation of weapon-relative motion and bones. Requires `HighFrequencyCamera=1` or `HighFrequencyCameraPosition=1`. |
| `MouseScalingFix` | `1` | Fixes rounding of small mouse movements, especially noticeable when using scoped weapons. |
| `MouseScaleQueryFix` | `1` | Makes `mousescale` without a value report the current setting without increasing it. |
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
AllowHighResolutionFonts=1
RawMouseInput=1
HighFrequencyCamera=1
HighFrequencyCameraPosition=1
HighFrequencyViewmodel=1
MouseScalingFix=1
MouseScaleQueryFix=1
AdaptiveScreenCenter=1
ScaleCursorClipToResolution=1
RestoreCursorClip=1

```

`AllowHighResolutionFonts` accepts square, 32-bit FNT0 pages of 256, 512, 1024,
2048 or 4096 pixels per side, with up to 16 equally sized pages per file. A
256x256 font can coexist with a 1024x1024 font. Detection uses the exact file
length reported by the game's loader for loose files and PFF resources; it runs
when a font loads, never while drawing text. Invalid tracked files are rejected.
Set the option to `0` and restart to retain the unmodified native font loader.

The supplied pixels are uploaded at their detected dimensions through the native
texture manager. Normalized UVs, reference width, spacing and logical text size
remain defined by the FNT. HD replacements must preserve those logical metrics
to retain the original layout. No vector fonts, sharpening, font substitution or
aspect-ratio correction are applied. The device's texture limits can reduce the
actual GPU texture resolution.

The native font loader also has an optional CPU drawing path which assumes a
256x256 pixel buffer. For HD files, the DLL stages a logical 256x256 mirror for
that path and redirects GPU uploads to the original HD pages. This avoids
misreading subsequent pages or changing CPU-buffer ownership. If an HD GPU upload
fails, the same logical mirror supplies a lower-resolution fallback. Original
256x256 FNTs use the original loading path without a staging copy.

See [dynamic FNT loading](docs/dynamic_fnt_loading.md) for format requirements,
implementation details and the automated/manual validation status.

`RawMouseInput` receives mouse packets on a dedicated message thread so movement
can reach presentation frames between the game's fixed simulation updates. Each
`WM_INPUT` event is read directly and then `GetRawInputBuffer` reads queued reports
in bounded batches. The buffered reader handles the different packet layout used
by a 32-bit game on 64-bit Windows (WOW64). Only relative movement uses Raw Input;
buttons, wheel and menu cursor coordinates keep the game's native Windows message
path. The registration neither captures activation clicks nor receives background
input, so other applications can be activated normally.
`HighFrequencyCamera` projects the game's native two-update mouse smoothing, then
uses the current sensitivity, vertical inversion, and scoped-view divisor. The
temporary visual angles are restored immediately after the camera is built; the
simulation consumes each movement once through its original path. The feature
currently applies to the local player in first person. Third-person, vehicle,
and spectator cameras retain their native behavior.

`HighFrequencyCameraPosition` captures the local player's position after verified
native movement producers and interpolates XYZ using the game's render phase.
This smooths the apparent movement of fixed scenery such as poles and walls
while strafing or walking diagonally. It leaves mouse yaw/pitch on the existing
Raw Input path. Only the private camera description used for rendering changes;
map assets, actor positions, collision, firing and networking retain their native
state. Native eye-height and camera offsets are preserved.

The option operates independently of `RawMouseInput` and `HighFrequencyCamera`.
It adds up to one movement interval of visual position lag: normally 4 ms for
the fast producer, or 16 ms for a supported slow producer. Disable it with
`HighFrequencyCameraPosition=0` to retain native camera translation. It does not
interpolate other players/vehicles, third-person cameras or skeletal animations
of world objects. Pauses, focus loss, target/mount changes, teleports, clock
discontinuities and unobserved actor motion invalidate its history. Unknown
executable bytes disable the option before any of its three hooks are written.

`HighFrequencyViewmodel` combines the presentation camera with interpolated
weapon-relative motion and bone transforms in a private palette. Position
smoothing is shared with the weapon, including when raw mouse rotation is off.
History is rebased across position discontinuities. Weapon simulation, firing
and networking remain native.

## Building

The project must be built as a 32-bit Windows DLL. With MinGW:

```sh
cmake -S . -B build -DCMAKE_TOOLCHAIN_FILE=mingw32-toolchain.cmake
cmake --build build
```

The build produces `winmm.dll` and `BHD Player Config.exe`. CMake also copies
`bhd_player_config.ini` into the build directory. A 64-bit build is rejected because
`dfbhd.exe` is 32-bit.

Run the regression suite with:

```sh
ctest --test-dir build --output-on-failure
```

The WinMM integration test imports the SDK's multimedia API normally and verifies
that the loader selects the built proxy. It checks all 193 named/ordinal exports,
concurrent clock calls, the game's nine WinMM APIs, timer callbacks and additional
wave/multimedia file calls against the system library. Run the math tests in
Debug to keep their assertions enabled.

## Compatibility

Before applying each modification, the DLL verifies the expected bytes in the executable. If the `dfbhd.exe` version is incompatible, the affected modification is not applied.

Patch initialization runs once from the supported game's verified startup
`timeGetTime`/`timeGetDevCaps` call sites (`0x00569F7A`/`0x00569FC5` return
addresses). Calls made by other DLLs during loading only forward to native
WinMM. `DllMain` performs no library loading, patch installation or thread waits.
An executable with different startup call sites needs explicit compatibility
support before these patches can initialize. No executable modification is
required for the supported version, which imports `WINMM.dll` directly.
