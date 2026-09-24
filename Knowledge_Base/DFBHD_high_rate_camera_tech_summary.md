# DFBHD High-Rate Mouse + Render Camera Patch

## Goal

Make Delta Force: Black Hawk Down feel responsive and visually smooth on modern high-refresh displays without increasing the game's simulation/logic tick rate.

Target architecture:

```text
High-rate Raw Input
        |
        v
Original 62.5 Hz gameplay/simulation
        |
        +---- committed mouse movement
        |
        v
Per-render camera prediction/correction
        |
        v
Smooth camera at render FPS
```

The important design rule is:

> Keep gameplay, physics, weapons, networking, timers, and `UpdateWorldCamera()` on the original logic tick. Only make the rendered camera consume mouse movement that arrived between logic ticks.

---

## Current DLL State

The current project already has a good Raw Input foundation in:

```text
src/raw_input.cpp
src/raw_input.h
```

Current behavior:

1. `WM_INPUT` receives high-frequency relative mouse packets.
2. Relative motion is accumulated in:
   - `g_accumX`
   - `g_accumY`
3. The DLL detours the game's `PollMouseInput()` at `0x005678F0`.
4. `RawPollMouseInput()` drains `g_accumX/g_accumY` and writes them to:
   - `0x00F655EC` = relative X
   - `0x00F655F0` = relative Y
5. The game still consumes those values only when its normal logic/input update runs.

Therefore Raw Input may receive 500/1000/2000+ Hz packets, but the camera can still visually update in roughly 16 ms steps if the logic update is approximately 62.5 Hz.

---

# Relevant Game Functions / Addresses

These addresses are for the currently supported `DFBHD.EXE` image base `0x00400000`.

## Mouse polling

```text
PollMouseInput
0x005678F0
```

The current DLL already detours this function.

Gameplay call site:

```text
0x004B49CB
```

Original bytes:

```text
E8 20 2F 0B 00
```

The logic loop calls `PollMouseInput()` here.

---

## UpdateWorldCamera

Decompiler name:

```text
sub_43BD80
```

Address:

```text
0x0043BD80
```

Logic-loop call site:

```text
0x004B4BBA
```

Original bytes:

```text
E8 C1 71 F8 FF
```

This function contains simulation-side camera state and fixed-per-call smoothing/filtering.

Examples found in the decompiled code include updates equivalent to fixed fractions such as:

```text
(value - current + 4) >> 2
```

Calling this whole function at render FPS changes the temporal behavior of those filters and risks affecting state that was designed to advance only with the logic tick.

### Do not move or duplicate the full `UpdateWorldCamera()` call into the render loop.

The previous experiment that did this produced the expected class of problems: altered camera distance/feel, bouncing, and inconsistent smoothness.

---

## CalculateCameraPositions

Decompiler name:

```text
sub_43B5E0
```

Address:

```text
0x0043B5E0
```

Render-loop call site:

```text
0x004B7297
```

Original bytes:

```text
E8 44 43 F8 FF
```

Immediately after that call, at `0x004B729C`, the game starts copying the calculated camera pose into locals used by the current render frame.

This makes `0x004B7297` the preferred hook point.

---

# Render Camera Globals

`CalculateCameraPositions()` writes the final render-camera pose to:

```text
0x007F2DD8 = position X
0x007F2DDC = position Y
0x007F2DE0 = position Z

0x007F2DE4 = yaw
0x007F2DE8 = pitch
0x007F2DEC = roll
```

The render loop copies them immediately after `CalculateCameraPositions()` returns.

For first-person mode, the function normally builds these values from the player/entity state, including player orientation.

---

# Recommended Hook Architecture

Add a new camera feature, for example:

```text
src/high_rate_camera.cpp
src/high_rate_camera.h
```

Add a patch-group option such as:

```ini
[PatchGroups]
HighRateCamera=1
```

Install the feature only after:

- executable image base validation succeeds;
- Raw Input installation succeeds;
- the expected call-site bytes match.

---

## Hook `CalculateCameraPositions()` at the call site

Patch:

```text
0x004B7297
```

from:

```asm
CALL 0x0043B5E0
```

to:

```asm
CALL CalculateCameraPositionsHook
```

The wrapper should conceptually be:

```cpp
using CalculateCameraPositionsFn = int(__cdecl*)();

constexpr uintptr_t kCalculateCameraPositions = 0x0043B5E0;

extern "C" int __cdecl CalculateCameraPositionsHook()
{
    const auto original =
        reinterpret_cast<CalculateCameraPositionsFn>(kCalculateCameraPositions);

    const int result = original();

    ApplyHighRateCameraCorrection();

    return result;
}
```

No trampoline is required for the original function itself because the wrapper can call `0x0043B5E0` directly.

Validate the original call-site bytes before patching.

---

# Raw Input State Needed for Prediction

The existing Raw Input backend currently stores only the motion that has not yet been consumed by `RawPollMouseInput()`.

Add cumulative counters so the renderer can distinguish:

```text
all physical motion received
vs.
motion already delivered to the game's logic tick
```

Suggested state:

```cpp
volatile LONG g_totalRawX = 0;
volatile LONG g_totalRawY = 0;

volatile LONG g_committedRawX = 0;
volatile LONG g_committedRawY = 0;
```

32-bit modular counters are sufficient because the only meaningful operation is a short-term difference between snapshots. Use interlocked operations.

---

## When processing WM_INPUT

For every accepted relative mouse packet:

```cpp
InterlockedExchangeAdd(&g_accumX, mouse.lLastX);
InterlockedExchangeAdd(&g_accumY, mouse.lLastY);

InterlockedExchangeAdd(&g_totalRawX, mouse.lLastX);
InterlockedExchangeAdd(&g_totalRawY, mouse.lLastY);
```

Do not increment the cumulative counters for:

- ignored absolute reports;
- the first movement intentionally discarded after focus recovery.

---

## When `RawPollMouseInput()` consumes movement

The existing code does:

```cpp
const LONG x = InterlockedExchange(&g_accumX, 0);
const LONG y = InterlockedExchange(&g_accumY, 0);
```

After the game-facing values are selected, record that they have been committed to the logic side:

```cpp
InterlockedExchangeAdd(&g_committedRawX, x);
InterlockedExchangeAdd(&g_committedRawY, y);
```

Then the renderer can calculate:

```cpp
residualX = totalRawX - committedRawX;
residualY = totalRawY - committedRawY;
```

This represents physical mouse movement received since the last movement handed to the game simulation.

---

## Reset synchronization

Reset or resynchronize cumulative state on:

- focus loss;
- focus regain;
- Raw Input suspend/resume;
- player/camera invalidation;
- map/loading transitions if necessary;
- feature enable/disable transitions.

Do not allow stale residual motion to survive a focus transition.

A safe reset is conceptually:

```cpp
totalRawX = 0;
totalRawY = 0;
committedRawX = 0;
committedRawY = 0;
```

at the same time the existing accumulated input state is cleared.

---

# Phase 1: Rotation Only

Implement only high-rate yaw/pitch correction first.

Do not initially predict:

- camera XYZ;
- roll;
- weapon state;
- player/entity state;
- physics;
- animation state.

The first milestone should modify only:

```text
0x007F2DE4 = render yaw
0x007F2DE8 = render pitch
```

after the original `CalculateCameraPositions()` has completed.

This isolates the experiment and minimizes the possibility of breaking gameplay.

---

# Critical Point: Raw Counts Are Not Directly Camera Angle Units

Do **not** add raw mouse counts directly to `0x007F2DE4` or `0x007F2DE8`.

The game has its own mouse scaling/input-action path.

The relevant game function is:

```text
sub_45FDF0
0x0045FDF0
```

The current `mouse_scaling_fix.cpp` already hooks a scaling block inside this function. In the supported binary the signature occurs around:

```text
0x0045FE82
```

The decompiled function shows the following important behavior:

```cpp
rawX = LOWORD-like 32-bit value from 0x00F655EC;
rawY = value from 0x00F655F0;

if (!dword_9F20E0)
    rawY = -rawY;

scale = dword_9F20E4 << 11;

if (special scoped/weapon conditions)
    scale /= sub_488260();

scaledX = (rawX * scale) >> 16;
scaledY = (rawY * scale) >> 16;
```

Relevant globals:

```text
0x009F20E0 = vertical mouse inversion state
0x009F20E4 = mouse scale/sensitivity source
```

The scaled X/Y values are then dispatched through the game's input/action system.

### Important

The currently supplied sources prove this scaling stage, but they do **not** yet prove the final multiplier or action mapping that converts the scaled mouse action into the player's 32-bit yaw/pitch angle units.

Do not invent a magic multiplier.

---

# Find the Exact Mouse-to-View Transform

Before considering the rotation patch complete, determine the exact relation between:

```text
raw physical counts
-> game-scaled mouse axis
-> player yaw/pitch delta
```

Useful player orientation fields observed by `CalculateCameraPositions()` are:

```text
NumberOfBytesRead + 20 = player/entity yaw
NumberOfBytesRead + 24 = player/entity pitch
```

The game's action dispatcher ultimately calls a player-specific callback through approximately:

```text
*(NumberOfBytesRead + 36)
callback at +328
```

That is a useful reverse-engineering target if an exact action-level transform is needed.

---

## Recommended instrumentation

Add temporary logging around a normal first-person gameplay tick.

For each logic tick log:

```text
raw_dx
raw_dy

scaled_dx
scaled_dy

player_yaw_before
player_yaw_after

player_pitch_before
player_pitch_after

camera_yaw_after_CalculateCameraPositions
camera_pitch_after_CalculateCameraPositions
```

Use wrap-aware 32-bit angle subtraction for yaw.

Test with:

1. normal first-person view;
2. horizontal-only movement;
3. vertical-only movement;
4. multiple sensitivity settings;
5. inverted and non-inverted Y;
6. scoped/zoomed view if applicable.

The result should reveal whether the final mapping is a simple constant multiplier or whether additional mode-specific behavior must be reproduced.

The production patch should reproduce the game's exact mapping rather than approximate it.

---

# Render Prediction Logic

Once the exact conversion is known:

```cpp
void ApplyHighRateCameraCorrection()
{
    if (!FeatureEnabled())
        return;

    if (!raw_input::IsEnabled())
        return;

    if (!ValidGameplayCamera())
        return;

    const MousePredictionSnapshot mouse =
        raw_input::GetPredictionSnapshot();

    const int32_t residualX =
        mouse.totalX - mouse.committedX;

    const int32_t residualY =
        mouse.totalY - mouse.committedY;

    if (residualX == 0 && residualY == 0)
        return;

    ViewDelta predicted =
        ConvertResidualMouseToGameAngles(residualX, residualY);

    *reinterpret_cast<volatile uint32_t*>(0x007F2DE4) += predicted.yaw;

    ApplyPitchSafely(predicted.pitch);
}
```

The function name `ConvertResidualMouseToGameAngles()` is intentional.

Keep all game-specific sensitivity, inversion, zoom, and angle-unit behavior inside that function.

---

# Angle Handling

DFBHD uses integer angle units rather than floating-point degrees for these camera/player values.

Use:

- 64-bit intermediates for multiplication;
- explicit `uint32_t` wrap behavior for yaw;
- the same pitch clamping/wrapping rules used by the player view code.

Do not clamp yaw as ordinary signed degrees.

Do not assume `1 unit == 1 degree`.

---

# Why the Prediction Should Not Double-Apply Input

Example:

```text
t = 0 ms
logic tick consumes mouse input A
committed total includes A
simulation/player yaw includes A

t = 3 ms
new raw input B arrives
total includes A+B
committed still contains A
render residual = B
camera renders simulation yaw + B prediction

t = 16 ms
next logic tick consumes B
committed now contains A+B
simulation/player yaw now officially includes B
render residual returns to 0
```

The visual result should remain continuous because movement transitions from:

```text
temporary render prediction
```

to:

```text
official simulation state
```

without applying the same counts twice.

---

# Do Not Modify Simulation Orientation for Prediction

The prediction stage must not write residual input into:

```text
NumberOfBytesRead + 20
NumberOfBytesRead + 24
```

or equivalent player/entity simulation fields.

Only modify the render-camera output generated by `CalculateCameraPositions()`.

This keeps:

- weapon direction logic;
- physics;
- animation;
- multiplayer state;
- networking;
- server-visible orientation;
- simulation timing

on the original game path.

If later testing proves that weapon rendering needs a matching visual-only orientation, handle that as a separate render-only layer.

---

# Why `0x004B7297` Is Better Than the Previous `0x004B745B` Test Point

The previous experiment used:

```text
0x004B745B
```

which calls:

```text
0x005382E0
```

and behaves like an empty/null function in this context.

That location can be useful as a generic code-cave style hook point, but it is too late for the cleanest camera correction.

At `0x004B7297`:

1. the game calls `CalculateCameraPositions()`;
2. the hook can adjust the resulting render pose;
3. starting at `0x004B729C`, the game immediately copies that pose into render-frame locals.

This is the preferred point.

---

# Phase 2: Smooth Camera Translation

After yaw/pitch prediction is stable, solve the remaining 62.5 Hz positional stepping separately.

The player/head/body position used by the camera can still advance only on logic ticks.

Do not solve this by running `UpdateWorldCamera()` more often.

Instead keep simulation snapshots:

```cpp
struct CameraSimulationSnapshot
{
    int32_t x;
    int32_t y;
    int32_t z;
    uint32_t yaw;
    int32_t pitch;
    LARGE_INTEGER timestamp;
};
```

Store at least:

```text
previous simulation snapshot
current simulation snapshot
```

Then derive a visual XYZ position every render frame.

Possible strategies:

### Third person

Prefer interpolation.

### First person

Test:

- very short extrapolation from the last two snapshots;
- or interpolation with minimum added latency.

Mouse rotation prediction should remain independent from XYZ smoothing.

---

# Time-Correct Filters

If reproducing any original fixed-per-tick smoothing in the render layer, convert it to a time-based coefficient.

For a filter equivalent to `1/4` correction every 16 ms:

```cpp
alpha = 1.0 - pow(0.75, dt / 0.016);
```

For a filter equivalent to `1/32` correction every 16 ms:

```cpp
alpha = 1.0 - pow(31.0 / 32.0, dt / 0.016);
```

This preserves approximately the same real-time response at:

```text
60 FPS
144 FPS
240 FPS
360 FPS
```

instead of making the filter stronger simply because it is called more often.

Use the game's measured real logic interval if later instrumentation shows a more accurate base value than `0.016`.

---

# Instrumentation / Diagnostics

Add `QueryPerformanceCounter`-based counters for:

```text
Raw Input reports
RawPollMouseInput calls
CalculateCameraPositionsHook calls
simulation camera-state changes
```

Useful periodic log:

```text
raw_reports_hz=1000
logic_mouse_polls_hz=62.5
camera_render_calls_hz=240
residual_dx=...
residual_dy=...
```

This makes it possible to verify that the architecture is actually:

```text
raw input rate != logic rate != render rate
```

and that the render camera is using residual input between logic ticks.

---

# Logging Rules

Do not log every `WM_INPUT` packet in normal production mode.

Per-packet logging can itself create timing noise.

Use:

- counters;
- accumulated values;
- periodic statistics;
- optional verbose diagnostics behind a separate debug setting.

---

# Failure / Safety Behavior

If any expected instruction bytes do not match:

```text
do not install the camera hook
log an error
leave the game unmodified
```

If Raw Input is disabled or unavailable:

```text
HighRateCamera should disable its raw-residual prediction path
```

Do not try to predict using stale values.

If the camera/player state is not valid:

```text
return without modifying camera globals
```

Examples include:

- menus;
- loading screens;
- no local player;
- transitions where `NumberOfBytesRead == 0`;
- unsupported camera modes until explicitly tested.

---

# Suggested Public Raw Input API

Example:

```cpp
namespace raw_input {

struct PredictionSnapshot
{
    LONG totalX;
    LONG totalY;
    LONG committedX;
    LONG committedY;
};

PredictionSnapshot GetPredictionSnapshot();
void ResetPredictionState();

}
```

The camera module should not access Raw Input module globals directly.

---

# Suggested Camera Module API

```cpp
namespace high_rate_camera {

struct Settings
{
    bool enabled;
};

bool Install(const Settings& settings);
void Reset();

}
```

`Install()` should:

1. validate expected bytes at `0x004B7297`;
2. install the relative CALL redirect;
3. log success/failure.

---

# Integration in `dinput8.cpp`

Extend `PatchConfig` with something like:

```cpp
bool highRateCamera = true;
```

Load:

```ini
[PatchGroups]
HighRateCamera=1
```

Install after Raw Input:

```cpp
const bool rawInstalled =
    raw_input::Install({config.rawMouseInput,
                        config.rawInputStatisticsIntervalMs});

high_rate_camera::Install({
    config.highRateCamera &&
    config.rawMouseInput &&
    rawInstalled
});
```

Exact ordering may be adjusted to match the existing project structure.

---

# Acceptance Criteria: Phase 1

The rotation patch is successful when all of the following are true:

1. Raw Input report frequency can be much higher than 62.5 Hz.
2. Logic/gameplay remains at the original rate.
3. `CalculateCameraPositionsHook()` executes at render FPS.
4. Moving the mouse between two logic ticks visibly changes render yaw/pitch.
5. There is no visible correction/jump when the next logic tick consumes the predicted counts.
6. Mouse sensitivity matches the original game.
7. Y inversion still works.
8. Scoped/zoom sensitivity still works.
9. No change to:
   - movement speed;
   - fire rate;
   - physics;
   - animation timing;
   - networking;
   - server behavior.
10. Disabling `HighRateCamera` restores original behavior.

---

# Acceptance Criteria: Phase 2

After positional smoothing:

1. stationary mouse + player movement no longer makes the camera position visibly step at approximately 62.5 Hz;
2. head/body translation is smooth at high render FPS;
3. camera latency is not noticeably increased;
4. third-person camera distance remains stable;
5. camera collision behavior remains correct;
6. the old bouncing/closer-camera behavior from repeatedly calling `UpdateWorldCamera()` does not return.

---

# Explicit Non-Goals

Do not:

- raise the full game logic tick to 125/240 Hz;
- repeatedly call `UpdateWorldCamera()` from the render loop;
- alter network tick behavior;
- alter server simulation;
- change weapon/fire timers to achieve smoothness;
- write raw prediction into player simulation yaw/pitch;
- hardcode an unverified raw-count-to-angle multiplier.

---

# Recommended Implementation Order

```text
1. Add cumulative Raw Input totals and committed totals.
2. Add QPC instrumentation.
3. Add `high_rate_camera` module.
4. Hook the CALL at 0x004B7297.
5. Initially make the hook call original code only and verify stability.
6. Log raw residual movement at render FPS.
7. Instrument the game's mouse-to-view conversion.
8. Determine the exact residual mouse -> yaw/pitch transform.
9. Apply yaw/pitch render prediction only.
10. Test normal / inverted / multiple sensitivities / zoom.
11. Harden state-reset and transition handling.
12. Add XYZ snapshot smoothing as a separate second phase.
```

---

# Most Important Architectural Summary

The existing Raw Input implementation is already capturing the mouse at high frequency.

The remaining issue is that the game only **consumes and converts that input into simulation/view state at the logic tick rate**.

The fix should therefore not make the entire game update faster.

Instead:

```text
Raw Input keeps collecting high-rate motion.

The normal logic tick consumes motion normally.

The renderer calculates:
    physical input received
    minus
    physical input already committed to simulation.

That residual is converted using the game's exact mouse/view transform
and applied only to the render camera after CalculateCameraPositions().

The next simulation tick consumes those same counts normally,
at which point the residual falls back to zero.
```

This should provide modern-feeling high-rate mouse response while preserving the original gameplay simulation.
