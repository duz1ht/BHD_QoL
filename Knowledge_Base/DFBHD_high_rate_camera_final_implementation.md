# DFBHD High-Rate Mouse / Render-Camera Prediction
## Final implementation handoff for the `dinput8.dll`

## Objective

Make Delta Force: Black Hawk Down feel like a modern high-refresh-rate FPS without increasing the game's simulation tick rate.

The final architecture should be:

```text
High-rate Raw Input
        |
        v
Raw mouse totals tracked continuously
        |
        +-----------------------------+
        |                             |
        v                             v
Original game logic             Render-only predictor
~62.5 Hz                        every rendered frame
        |                             |
        v                             |
Official player orientation           |
        |                             |
        +-----------> visual camera <-+
```

The simulation, networking, weapon logic, physics, timers, and original gameplay update rate should remain unchanged.

The render camera should receive a temporary prediction for mouse movement that arrived after the most recent logic tick.

---

# Core conclusion

Do **not**:

- raise the entire game logic rate;
- call `UpdateWorldCamera()` every render frame;
- write predicted input into the real player orientation;
- directly add raw mouse counts to camera yaw/pitch;
- invent a `raw_count * magic_constant` conversion.

The decompiled executable shows that DFBHD has a real mouse-to-view pipeline with:

- a first mouse scaling/remainder stage;
- sensitivity and Y inversion;
- zoom-dependent sensitivity;
- action dispatch;
- separate pitch/yaw action variables;
- a stateful yaw filter;
- additional pitch scaling;
- rotation-matrix composition;
- conversion back to yaw/pitch.

The predictor should reproduce the **mouse-caused portion** of that pipeline on temporary state and apply the result only to the camera used for rendering.

---

# Confirmed game pipeline

The relevant path in the current supported executable is:

```text
Raw mouse movement
    |
    v
sub_5678F0
PollMouseInput
0x005678F0
    |
    v
sub_4B4970
logic/input update
    |
    |  first scaling / previous-value state
    v
qword_F655EC
    |
    v
sub_45FDF0
mouse input dispatch
    |
    |  sensitivity
    |  Y inversion
    |  zoom scaling
    v
scaled X / scaled Y
    |
    v
sub_45F8C0
action dispatcher
    |
    v
player callback at object + 328
    |
    v
sub_435120
player/pilot input callback
    |
    +--> word_872384 = pitch input
    |
    +--> word_87238E = yaw input
    |
    v
sub_438150
player update
    |
    +--> player + 120 = yaw rotation input
    |
    +--> player + 124 = pitch rotation input
    |
    v
sub_433210
orientation update
    |
    v
sub_55FE00
build/apply rotation matrix
    |
    v
sub_55E450
matrix -> pose angles
    |
    v
player orientation at +20 / +24
    |
    v
sub_43B5E0
CalculateCameraPositions
    |
    v
render camera yaw / pitch
```

---

# Relevant addresses

These addresses are for the currently supported `DFBHD.EXE` with image base `0x00400000`.

```text
PollMouseInput                   0x005678F0
Game logic/input update          0x004B4970

Mouse scaling/action dispatch    0x0045FDF0
Player input callback            0x00435120
Player movement/view update      0x00438150
Orientation matrix update        0x00433210

CalculateCameraPositions         0x0043B5E0
UpdateWorldCamera                0x0043BD80
```

Known call sites:

```text
PollMouseInput logic call        0x004B49CB
UpdateWorldCamera logic call     0x004B4BBA
CalculateCameraPositions call    0x004B7297
```

The preferred render hook remains:

```text
0x004B7297
```

The original instruction is a relative `CALL` to:

```text
0x0043B5E0
```

The hook should call the original `CalculateCameraPositions()` first, then apply the visual prediction before returning.

Immediately after this call the render loop copies the calculated camera state for the current frame.

---

# Render-camera globals

The final camera pose produced by `CalculateCameraPositions()` is stored at:

```text
0x007F2DD8 = X
0x007F2DDC = Y
0x007F2DE0 = Z

0x007F2DE4 = yaw
0x007F2DE8 = pitch
0x007F2DEC = roll
```

The first implementation should modify only yaw/pitch.

Do not touch X/Y/Z until rotation prediction is stable.

---

# Important player fields

For the local player/entity, the relevant orientation values observed by the camera code are approximately:

```text
player + 20 = yaw
player + 24 = pitch
player + 28 = roll
```

The view/movement update uses temporary rotation-control fields:

```text
player + 120 = yaw rotation control
player + 124 = pitch rotation control
player + 128 = roll rotation control
```

`sub_433210()` later consumes these values approximately as:

```cpp
yawStep   = (playerYawControl   + 2) >> 2;
pitchStep = (playerPitchControl + 2) >> 2;
rollStep  = (playerRollControl  + 2) >> 2;

sub_55FE00(playerMatrix, yawStep, pitchStep, rollStep);
sub_55E450(playerPose, playerMatrix);
```

The render predictor should use equivalent math on temporary copies instead of modifying the real player.

---

# Raw Input tracking

The current DLL already accumulates relative mouse movement for `RawPollMouseInput()`.

Extend the Raw Input module with cumulative counters.

Suggested state:

```cpp
namespace raw_input {

struct PredictionSnapshot
{
    LONG totalX;
    LONG totalY;
    LONG committedX;
    LONG committedY;
};

}
```

Internally:

```cpp
volatile LONG g_totalRawX = 0;
volatile LONG g_totalRawY = 0;

volatile LONG g_committedRawX = 0;
volatile LONG g_committedRawY = 0;
```

When an accepted relative `WM_INPUT` packet arrives:

```cpp
InterlockedExchangeAdd(&g_accumX, mouse.lLastX);
InterlockedExchangeAdd(&g_accumY, mouse.lLastY);

InterlockedExchangeAdd(&g_totalRawX, mouse.lLastX);
InterlockedExchangeAdd(&g_totalRawY, mouse.lLastY);
```

When `RawPollMouseInput()` drains movement for the real game:

```cpp
LONG x = InterlockedExchange(&g_accumX, 0);
LONG y = InterlockedExchange(&g_accumY, 0);

// write x/y to the game as already done

InterlockedExchangeAdd(&g_committedRawX, x);
InterlockedExchangeAdd(&g_committedRawY, y);
```

At render time:

```cpp
residualRawX = totalRawX - committedRawX;
residualRawY = totalRawY - committedRawY;
```

This residual is the movement physically received since the most recent mouse data delivered to the simulation.

Use modular 32-bit subtraction.

---

# Reset rules

Prediction state must be reset or resynchronized on:

- focus loss;
- focus regain;
- Raw Input suspend/resume;
- loading transitions;
- map changes;
- local-player destruction/recreation;
- entering menus;
- leaving gameplay;
- feature enable/disable;
- any detected invalid camera/player state.

Reset together with the existing Raw Input accumulator:

```cpp
totalRawX = 0;
totalRawY = 0;
committedRawX = 0;
committedRawY = 0;
```

Never allow pre-focus-loss residual movement to affect the camera after focus returns.

---

# Stage 1 of the game's mouse path

Inside `sub_4B4970`, after `PollMouseInput()`, the normal active-input path performs approximately:

```cpp
v1 = 4 * rawX / dword_A02248;
v2 = 4 * rawY / dword_A02248;

dword_9FB14C = 4 * rawX - v1 * (dword_A02248 - 1);
dword_9F528C = v1;

dword_9F5294 = 4 * rawY - v2 * (dword_A02248 - 1);
dword_9FB290 = v2;

mouseX = v1 + dword_9F3750;
mouseY = v2 + dword_9FB280;

dword_9F3750 = v1;
dword_9FB280 = v2;
```

There are alternate branches depending on game/input state.

The render predictor must **not write** these globals.

Instead, create a snapshot and reproduce the calculation locally.

Suggested structure:

```cpp
struct MouseLogicState
{
    int sensitivityDivisor;   // dword_A02248

    int previousX;            // dword_9F3750
    int previousY;            // dword_9FB280

    int remainderX;           // dword_9FB14C
    int remainderY;           // dword_9F5294

    bool normalPollingPath;
};
```

Only enable prediction when the same normal gameplay/input conditions used by the real game are active.

If the game is in an alternate input branch, return without prediction until that state is explicitly supported.

---

# Critical prediction rule: simulate WITH and WITHOUT residual input

The mouse pipeline has state, especially the yaw filter.

Therefore do not treat the predicted next value itself as the visual correction.

For each render frame calculate two temporary branches from the exact same current game state:

```text
A = next-step result if no new residual mouse existed
B = next-step result if the current residual mouse were consumed now
```

Then isolate the input-caused contribution:

```text
mouse-only correction = B - A
```

This cancels state evolution that the game would have performed even with no new mouse input.

Conceptually:

```cpp
PredictionResult zero =
    SimulateMouseLook(snapshot, 0, 0);

PredictionResult withResidual =
    SimulateMouseLook(snapshot, residualRawX, residualRawY);

PredictionResult delta =
    Difference(withResidual, zero);
```

Do not mutate `snapshot`.

Do not mutate game globals.

---

# Stage 2: sensitivity, inversion, and zoom

`sub_45FDF0()` takes the mouse values produced by the previous stage and applies another scaling stage.

The important logic is approximately:

```cpp
mouseX = inputX;
mouseY = inputY;

if (!dword_9F20E0)
    mouseY = -mouseY;

scale = dword_9F20E4 << 11;

if (zoom_or_scoped_conditions)
    scale /= sub_488260();

scaledX = (mouseX * scale) >> 16;
scaledY = (mouseY * scale) >> 16;
```

Relevant globals/functions:

```text
0x009F20E0 = Y inversion state
0x009F20E4 = mouse scaling/sensitivity source

sub_488260 = zoom-related divisor used by this path
```

Use 64-bit intermediates exactly as the game does.

The existing DLL already contains mouse scaling related work. Prefer sharing the same helper/logic between the existing fix and the render predictor instead of duplicating different formulas.

Example:

```cpp
struct ScaledMouse
{
    int32_t x;
    int32_t y;
};

ScaledMouse ScaleMouseLikeGame(
    int32_t logicMouseX,
    int32_t logicMouseY,
    const MouseScaleState& state);
```

The predictor must use the current real inversion/sensitivity/zoom state.

---

# Stage 3: action mapping

The scaled mouse axes are dispatched through the game's input-action system.

The player's normal callback is:

```text
sub_435120
0x00435120
```

The executable installs this function as the `"pilot"` input callback and the generic dispatcher calls the function pointer at object offset `+328`.

The important action IDs handled by `sub_435120()` are:

```text
259 -> word_872384 = +a3
260 -> word_872384 = -a3

263 -> word_87238E = +a3
264 -> word_87238E = -a3
```

Therefore:

```text
word_872384 = pitch look input
word_87238E = yaw look input
```

There are additional related cases, so do not replace the whole game's input callback.

The predictor only needs to derive the temporary pitch/yaw look values produced by the current mouse bindings.

---

# Binding-mapping implementation

Do not assume forever that:

```text
X always means yaw
Y always means pitch
```

The normal configuration behaves that way, but `sub_45FDF0()` uses the game's binding/action table.

Recommended implementation:

Create a small helper that mirrors only the mouse-axis-to-look-action part of `sub_45FDF0()`.

It should resolve the currently active binding for the two mouse axes and produce:

```cpp
struct LookActions
{
    int16_t pitchInput; // equivalent temporary word_872384
    int16_t yawInput;   // equivalent temporary word_87238E
};
```

The helper must support the sign represented by action IDs:

```text
259 / 260
263 / 264
```

If fully mirroring the binding table is too large for the first implementation, support the standard mouse-look mapping first, but:

1. keep it isolated in `ResolveMouseLookActions()`;
2. log the chosen mapping;
3. validate it against real `sub_435120()` calls;
4. do not bury the mapping in camera code.

The production target should mirror the current binding state.

---

# Yaw path discovered in `sub_438150`

Yaw is stateful.

The game approximately performs:

```cpp
int16_t oldYawFilterState = *(int16_t*)(player + 432);

int16_t sum =
    yawInput + oldYawFilterState;

int16_t filtered =
    sum - ((sum + 4) >> 3);

*(int16_t*)(player + 432) = filtered;

int32_t yawControl =
    3072 * filtered;

*(int32_t*)(player + 120) = yawControl;
```

The exact storage path in the decompiled function is:

```text
v2 = player + 396
filter state = *(int16_t *)(v2 + 36)
             = *(int16_t *)(player + 432)
```

The render predictor must read the current real filter state but must **not update it**.

Implement a pure helper:

```cpp
int32_t PredictYawControl(
    int16_t yawInput,
    int16_t currentFilterState)
{
    int32_t sum =
        (int32_t)yawInput +
        (int32_t)currentFilterState;

    int16_t filtered =
        (int16_t)(sum - ((sum + 4) >> 3));

    return 3072 * (int32_t)filtered;
}
```

Also calculate the zero-input branch:

```cpp
yawWithMouse =
    PredictYawControl(predictedYawInput, filterState);

yawWithoutMouse =
    PredictYawControl(0, filterState);

deltaYawControl =
    yawWithMouse - yawWithoutMouse;
```

This is important.

Using only `yawWithMouse` would incorrectly predict filter decay/state progression as if it were mouse movement.

---

# Pitch path discovered in `sub_438150`

Pitch does not use the same yaw filter.

The important base transformation is:

```cpp
pitchControl =
    pitchInput << 14;
```

Depending on game state, additional scaling is applied.

One observed normal path performs approximately:

```cpp
pitchControl =
    (135168LL * pitchControl) >> 16;
```

There are conditional branches controlled by game state, including:

```text
dword_9F2174
dword_9F2178
```

There are also special adjustments in some player states.

Do not assume the simple shift is correct in every mode.

Implement:

```cpp
int32_t PredictPitchControl(
    int16_t pitchInput,
    const PlayerLookState& state);
```

Mirror the pitch-specific conditions from `sub_438150()` that are active for the supported first-person gameplay mode.

As with yaw, calculate:

```cpp
pitchWithMouse =
    PredictPitchControl(predictedPitchInput, state);

pitchWithoutMouse =
    PredictPitchControl(0, state);

deltaPitchControl =
    pitchWithMouse - pitchWithoutMouse;
```

This automatically cancels non-mouse baseline/recenter terms if they are reproduced in both branches.

---

# Do not reproduce all of `sub_438150`

`sub_438150()` is a large player update function.

Do not call it from the render loop.

Do not clone the entire function.

Only mirror the minimum look-related calculations required to obtain the mouse-caused yaw/pitch control delta.

The goal is:

```text
read real state
simulate look math locally
produce delta
discard temporary state
```

---

# Convert predicted control into orientation using the game's math

The real game later consumes:

```text
player +120
player +124
player +128
```

in `sub_433210()`.

The relevant step is approximately:

```cpp
yawStep   = (yawControl   + 2) >> 2;
pitchStep = (pitchControl + 2) >> 2;
rollStep  = (rollControl  + 2) >> 2;
```

For render prediction use the mouse-only control deltas:

```cpp
predYawStep =
    (deltaYawControl + 2) >> 2;

predPitchStep =
    (deltaPitchControl + 2) >> 2;
```

Roll should remain zero for this feature:

```cpp
predRollStep = 0;
```

---

# Preferred final orientation method: temporary matrix

Do not directly treat `predYawStep` and `predPitchStep` as the final camera yaw/pitch values.

The game applies these values through rotation matrices.

The relevant math routines are:

```text
sub_55FE00
0x0055FE00

sub_55E450
0x0055E450
```

`sub_55FE00()` builds/applies the rotation using the game's fixed-point angle representation.

`sub_55E450()` converts a matrix back into pose/orientation values.

Use them on temporary memory only.

---

# Temporary orientation prediction

At render time, after the original `CalculateCameraPositions()`:

1. obtain the local player's current orientation/matrix;
2. copy the current orientation matrix to a local temporary buffer;
3. apply only the predicted mouse rotation to that temporary matrix;
4. convert the resulting temporary matrix back to yaw/pitch;
5. write only the predicted yaw/pitch to the render-camera globals;
6. discard all temporary state.

Conceptually:

```cpp
void ApplyHighRateCameraPrediction()
{
    if (!CanPredict())
        return;

    RawPredictionSnapshot raw =
        raw_input::GetPredictionSnapshot();

    int32_t residualX =
        raw.totalX - raw.committedX;

    int32_t residualY =
        raw.totalY - raw.committedY;

    if (residualX == 0 && residualY == 0)
        return;

    GamePredictionSnapshot state =
        CaptureGamePredictionState();

    LookPrediction zero =
        SimulateLook(state, 0, 0);

    LookPrediction moved =
        SimulateLook(state, residualX, residualY);

    int32_t deltaYawControl =
        moved.yawControl - zero.yawControl;

    int32_t deltaPitchControl =
        moved.pitchControl - zero.pitchControl;

    int32_t yawStep =
        (deltaYawControl + 2) >> 2;

    int32_t pitchStep =
        (deltaPitchControl + 2) >> 2;

    PredictedPose pose =
        ApplyTemporaryOrientationDelta(
            state,
            yawStep,
            pitchStep);

    RenderCameraYaw() = pose.yaw;
    RenderCameraPitch() = pose.pitch;
}
```

---

# How to apply the temporary matrix safely

Create local storage large enough for the matrix representation used by the player's orientation code.

Do **not** pass pointers to the real player matrix into a function that can mutate it.

Example structure:

```cpp
struct GameMatrix
{
    int32_t m[12]; // verify exact required size from current code
};

struct GamePose
{
    int32_t x;
    int32_t y;
    int32_t z;
    uint32_t yaw;
    int32_t pitch;
    int32_t roll;
};
```

Pseudo-flow:

```cpp
GameMatrix predictedMatrix =
    CopyCurrentPlayerMatrix();

GameMatrix deltaMatrix = IdentityMatrix();

// or use a temp pose/matrix layout exactly compatible with the game helpers

sub_55FE00(
    predictedMatrix.data(),
    yawStep,
    pitchStep,
    0);

GamePose predictedPose =
    ConvertMatrixToPose(predictedMatrix);
```

Important:

`sub_55FE00()` uses in-place matrix math through another matrix helper.

Verify the expected layout before calling it from the DLL.

Do not guess the matrix size or orientation order.

If calling the game's original math routines is awkward, reproduce their fixed-point math in the DLL exactly.

Calling the original pure math helpers is preferable if their inputs/outputs are confirmed.

---

# Alternative differential matrix method

If testing reveals that applying only:

```text
deltaYawControl
deltaPitchControl
```

to the current matrix is not exact enough because of rotation-order interactions, use a two-branch matrix method.

Build:

```text
M0 = temporary next orientation with zero residual mouse
M1 = temporary next orientation with residual mouse
```

Then derive the relative mouse-only rotation:

```text
C = M1 * inverse(M0)
```

and apply `C` to the current render orientation.

This is more mathematically exact but more complex.

Do not start here unless the simpler delta-control method shows measurable mismatch at logic-tick boundaries.

---

# Render hook

Implement a wrapper at the render call site:

```text
0x004B7297
```

Conceptually:

```cpp
using CalculateCameraPositionsFn =
    int(__cdecl*)();

static constexpr uintptr_t
    kCalculateCameraPositions = 0x0043B5E0;

extern "C"
int __cdecl CalculateCameraPositionsHook()
{
    auto original =
        reinterpret_cast<CalculateCameraPositionsFn>(
            kCalculateCameraPositions);

    int result = original();

    high_rate_camera::ApplyPrediction();

    return result;
}
```

Patch only the `CALL` instruction at the call site.

Before patching:

- validate the module image base;
- validate the expected original bytes;
- fail safely if they do not match.

---

# Why this hook point is correct

The order becomes:

```text
render frame
    |
    v
original CalculateCameraPositions()
    |
    v
official 62.5 Hz-derived camera pose exists
    |
    v
ApplyPrediction()
    |
    v
camera yaw/pitch now include uncommitted Raw Input
    |
    v
game copies camera values for this render frame
```

The simulation is never modified.

---

# Do not use the previous late hook point

A previous experiment used:

```text
0x004B745B
```

That location is useful as a generic hook/code-cave test point but occurs after the preferred camera calculation/copy region.

Use:

```text
0x004B7297
```

for this feature.

---

# Do not call `UpdateWorldCamera()` in the render loop

`UpdateWorldCamera()`:

```text
0x0043BD80
```

belongs to the logic path and contains stateful fixed-per-call smoothing.

Calling it at 144/240/360 FPS changes the real-time behavior of those filters and duplicates updates that still occur in the normal logic loop.

This caused the previously observed symptoms such as:

- camera distance changing;
- bouncing;
- inconsistent smoothness;
- alternating smooth/rough feel.

Leave it completely on the original logic path.

---

# Prediction state snapshot

Suggested render-time state object:

```cpp
struct GamePredictionSnapshot
{
    // Raw/game mouse stage
    int32_t mouseDivisor;
    int32_t previousMouseX;
    int32_t previousMouseY;

    // Sensitivity/scaling
    int32_t mouseScale;
    bool invertY;
    bool zoomScalingActive;
    int32_t zoomDivisor;

    // Action mapping
    MouseLookBinding lookBinding;

    // Player look state
    int16_t yawFilterState;
    bool pitchModeA;
    bool pitchModeB;

    // Player orientation
    GameMatrix playerMatrix;
    GamePose playerPose;

    // validity
    bool valid;
};
```

Keep this module read-only with respect to game state.

---

# Pure simulation API

Recommended internal design:

```cpp
namespace high_rate_camera {

struct LookPrediction
{
    int32_t yawControl;
    int32_t pitchControl;
};

LookPrediction SimulateLook(
    const GamePredictionSnapshot& state,
    int32_t rawDx,
    int32_t rawDy);

void ApplyPrediction();

}
```

`SimulateLook()` should be deterministic and have no side effects.

Internally:

```text
raw residual
    |
    v
SimulateLogicMouseStage()
    |
    v
ScaleMouseLikeGame()
    |
    v
ResolveMouseLookActions()
    |
    v
PredictYawControl()
PredictPitchControl()
```

---

# Recommended helper breakdown

```cpp
LogicMouse SimulateLogicMouseStage(
    const GamePredictionSnapshot& s,
    int32_t rawDx,
    int32_t rawDy);

ScaledMouse ScaleMouseLikeGame(
    const GamePredictionSnapshot& s,
    LogicMouse input);

LookActions ResolveMouseLookActions(
    const GamePredictionSnapshot& s,
    ScaledMouse input);

int32_t PredictYawControl(
    const GamePredictionSnapshot& s,
    int16_t yawInput);

int32_t PredictPitchControl(
    const GamePredictionSnapshot& s,
    int16_t pitchInput);

PredictedPose ApplyTemporaryOrientationDelta(
    const GamePredictionSnapshot& s,
    int32_t deltaYawControl,
    int32_t deltaPitchControl);
```

This separation is important for debugging.

---

# Logic-boundary continuity

The expected behavior is:

```text
logic tick T:
    game consumes mouse A
    committedRaw = A
    official player pose contains A

3 ms later:
    physical mouse B arrives
    totalRaw = A+B
    committedRaw = A
    residual = B

render:
    temporary predictor simulates effect of B
    visual camera = official pose + predicted B

next logic tick:
    game officially consumes B
    committedRaw = A+B
    official player pose now contains B

next render:
    residual = 0
    prediction disappears
    official pose has replaced it
```

There should be no visible snap if the predictor matches the game's look math.

A snap exactly when the logic tick occurs means the predicted transform does not match the real transform.

Treat that snap as the primary correctness diagnostic.

---

# Instrumentation

Add temporary diagnostics using `QueryPerformanceCounter`.

Track:

```text
WM_INPUT reports / second
RawPollMouseInput calls / second
CalculateCameraPositionsHook calls / second

residual raw X/Y
logic-stage predicted X/Y
scaled predicted X/Y
predicted pitch/yaw action values

deltaYawControl
deltaPitchControl

predicted camera yaw/pitch
official camera yaw/pitch
```

Useful periodic line:

```text
raw=1000.2Hz logic=62.5Hz render=239.8Hz
residual=(3,-1)
action=(pitch:-2,yaw:5)
controlDelta=(...)
cameraDelta=(...)
```

Do not log every Raw Input packet in production.

High-frequency file logging can create its own timing problems.

---

# Validation hook for action mapping

During development it is useful to observe calls to:

```text
sub_435120
```

for action IDs:

```text
259
260
263
264
```

Record:

```text
action id
a2
a3
current scaled X/Y
```

Use this to verify that `ResolveMouseLookActions()` reproduces the real game's mapping.

This diagnostic hook should be removable/disabled in production.

---

# Validation hook for final orientation

At each real logic update, record:

```text
player yaw before
player yaw after

player pitch before
player pitch after

yaw filter state before/after

real word_872384
real word_87238E

real player+120
real player+124
```

Compare those values against what `SimulateLook()` would have predicted using the same mouse input.

The predictor should first be validated offline against real logic ticks before enabling per-render camera correction.

---

# Development milestone 1: predictor verification only

Before changing the camera:

1. implement raw total/committed counters;
2. implement game-state snapshot;
3. implement `SimulateLook()`;
4. on a real logic tick, compare predicted values against actual values;
5. log mismatches;
6. do not modify render camera yet.

Success condition:

```text
predicted action values match real values
predicted yaw control matches real player+120
predicted pitch control matches real player+124
```

under supported first-person states.

This is the safest way to verify the reverse-engineered pipeline.

---

# Development milestone 2: rotation-only render prediction

Once milestone 1 matches:

1. install the `0x004B7297` render hook;
2. call original `CalculateCameraPositions()`;
3. calculate current residual Raw Input;
4. run zero and residual prediction branches;
5. obtain mouse-only yaw/pitch control delta;
6. apply that delta to a temporary orientation;
7. overwrite only:

```text
0x007F2DE4
0x007F2DE8
```

Do not modify:

```text
0x007F2DD8
0x007F2DDC
0x007F2DE0
0x007F2DEC
```

during this milestone.

---

# Development milestone 3: state coverage

Test and support:

- normal first person;
- inverted Y;
- all mouse sensitivity values;
- scoped view;
- zoomed weapons;
- crouch/prone if look math changes;
- death/spectator transitions;
- vehicle/turret modes separately;
- menus and map screens;
- window focus transitions.

If a mode is not understood, disable prediction in that mode rather than guessing.

---

# Development milestone 4: positional smoothing

Yaw/pitch prediction solves high-rate look response.

Player/head/body translation can still visually update at the logic rate.

Handle XYZ separately after rotation is correct.

Do not extend the mouse predictor into physics.

Store simulation camera snapshots:

```cpp
struct CameraSnapshot
{
    int32_t x;
    int32_t y;
    int32_t z;

    uint32_t yaw;
    int32_t pitch;
    int32_t roll;

    int64_t qpc;
};
```

Use:

```text
previous logic snapshot
current logic snapshot
```

Then create render-only XYZ smoothing.

Recommended starting point:

```text
third person -> interpolation

first person -> short bounded extrapolation
                or carefully tuned interpolation
```

Mouse yaw/pitch prediction should remain a separate layer.

---

# Time-correct smoothing

If reproducing any fixed-per-logic-tick filter at render rate, convert it to a time-based coefficient.

For a filter that keeps 75% of the previous error every 16 ms:

```cpp
alpha =
    1.0 - pow(0.75, dt / 0.016);
```

For a `31/32` retention filter:

```cpp
alpha =
    1.0 - pow(31.0 / 32.0, dt / 0.016);
```

Measure the actual logic interval and replace `0.016` if needed.

Do not simply execute a fixed per-tick filter more often.

---

# Feature configuration

Suggested config:

```ini
[PatchGroups]
RawMouseInput=1
HighRateCamera=1
```

Optional debug config:

```ini
[Debug]
HighRateCameraStats=0
HighRateCameraVerbose=0
```

`HighRateCamera` should require Raw Input.

If Raw Input fails to install:

```text
disable HighRateCamera
log the reason
continue running the game normally
```

---

# Suggested source files

```text
src/high_rate_camera.h
src/high_rate_camera.cpp

src/game_mouse_prediction.h
src/game_mouse_prediction.cpp
```

Keep responsibilities separated:

```text
raw_input.*
    physical mouse capture
    cumulative totals
    committed totals

game_mouse_prediction.*
    pure reproduction of DFBHD mouse/look math

high_rate_camera.*
    render hook
    validity checks
    temporary orientation
    camera output
```

---

# Safety and patch validation

For every patch:

1. verify the module base;
2. verify expected original bytes;
3. patch only if matched;
4. log failure;
5. leave the executable untouched on mismatch.

Do not silently patch unknown versions of `DFBHD.EXE`.

---

# Thread-safety

Raw Input counters may be updated from the window/message path while the render hook reads them.

Use interlocked operations for cumulative counters.

When reading a snapshot, read all four counters in a consistent way.

Exact atomicity across all four values is not essential if the worst case is one packet appearing on the next render frame, but never use undefined data races.

Game-state fields should be read from the game/render thread only unless proven otherwise.

---

# Pitch/yaw integer behavior

The game uses fixed-point/integer angles.

Preserve:

- signed 16-bit truncation where the original code uses `__int16`;
- signed right shifts;
- 32-bit wrap semantics;
- 64-bit multiplication intermediates;
- original rounding constants such as `+2` and `+4`.

Do not convert the core predictor to floating point.

A one-unit rounding difference can create a tiny periodic correction at each logic boundary.

---

# Failure conditions

Return without prediction if any of these are true:

```text
Raw Input disabled
HighRateCamera disabled
local player pointer invalid
camera pointer invalid
not in supported gameplay state
input not in the normal supported mouse path
loading/menu transition
binding mapping unresolved
unsupported vehicle/spectator mode
```

Fail closed.

An unsupported mode with original 62.5 Hz camera behavior is better than corrupting game state.

---

# Phase 1 acceptance criteria

The implementation is correct when:

1. the physical Raw Input rate can be 500/1000/2000 Hz;
2. the real game logic remains approximately 62.5 Hz;
3. `CalculateCameraPositionsHook()` runs at render FPS;
4. camera yaw/pitch respond to mouse packets arriving between logic ticks;
5. there is no visible snap when the next logic tick consumes the same mouse movement;
6. sensitivity matches original DFBHD exactly;
7. Y inversion matches original DFBHD;
8. scoped/zoom sensitivity matches original DFBHD;
9. disabling `HighRateCamera` restores original behavior;
10. multiplayer/gameplay state is unchanged.

---

# Gameplay invariants

The patch must not change:

```text
player simulation position
server-visible orientation
weapon fire direction logic
fire rate
reload timing
physics
movement speed
animation timing
network tick rate
server tick rate
game timers
```

Only the rendered camera is predicted.

---

# Primary debugging symptom guide

## Camera moves but snaps every ~16 ms

The prediction transform does not exactly match the real logic transform.

Check:

```text
first raw scaling stage
sensitivity divisor
Y inversion
zoom divisor
action mapping
yaw filter state
pitch conditional scaling
integer rounding
matrix rotation order
```

---

## Camera sensitivity is wrong but no snap

Likely error in:

```text
sub_45FDF0 scaling reproduction
zoom handling
binding sign
```

---

## Camera continues moving after mouse stops

Likely applying the full predicted yaw filter output instead of:

```text
withResidual - zeroResidual
```

Use the differential prediction.

---

## Camera affects gameplay/server direction

The implementation is writing into real player state.

Stop writing to:

```text
player +20
player +24
player +120
player +124
player matrix
```

Prediction must use temporary copies only.

---

## Camera distance/bounce changes

`UpdateWorldCamera()` or another simulation camera routine is probably being called from the render path.

Remove it.

---

# Recommended implementation order for Codex

```text
1. Add cumulative total/committed Raw Input counters.

2. Add game-state address/constants module.

3. Implement state validity checks.

4. Implement CaptureGamePredictionState().

5. Implement SimulateLogicMouseStage().

6. Implement ScaleMouseLikeGame().

7. Implement ResolveMouseLookActions().

8. Implement PredictYawControl() including player+432 filter state.

9. Implement PredictPitchControl() with the relevant sub_438150 branches.

10. Add debug comparison against real logic-tick outputs.

11. Make predictor match real player+120/player+124 exactly.

12. Implement temporary orientation/matrix application.

13. Validate predicted pose against real next-tick pose for controlled mouse input.

14. Hook the CALL at 0x004B7297.

15. Call original CalculateCameraPositions() first.

16. Apply render-only yaw/pitch prediction.

17. Test logic-boundary continuity at 60/144/240/360 FPS.

18. Test sensitivity, invert Y, zoom/scopes.

19. Harden focus/loading/mode reset behavior.

20. Only after rotation is finished, implement XYZ render smoothing.
```

---

# Final architectural summary

The DLL should not make DFBHD's simulation run faster.

It should do this:

```text
Raw Input continuously receives mouse movement at high frequency.

The real DFBHD logic still consumes mouse normally at ~62.5 Hz.

The DLL remembers how much physical mouse movement has not yet been
committed to that logic tick.

On every render frame:

    1. run the game's original CalculateCameraPositions();

    2. snapshot the current real mouse/player state;

    3. simulate DFBHD's mouse pipeline twice on temporary state:
           a) zero residual mouse
           b) current residual mouse

    4. subtract the two results to isolate only the effect of
       newly arrived mouse input;

    5. apply that mouse-only rotation through the game's orientation
       math on a temporary matrix;

    6. write the resulting yaw/pitch only to the render-camera globals;

    7. render the frame.

When the next real logic tick consumes those Raw Input counts,
the residual becomes zero and the official player orientation
replaces the temporary prediction without a visible jump.
```

That is the target implementation.

The key design principle is:

> **Predict presentation, never simulation.**
