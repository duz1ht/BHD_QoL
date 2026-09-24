# DFBHD dinput8.dll Unified High-Frequency Visual Pipeline

## Purpose

This document combines three related patches into one coherent implementation plan for a `dinput8.dll` loaded beside `DFBHD.EXE`:

1. `FreeRateMousePoll`
2. `HighRateCameraRotation`
3. `HighFrequencyVisualCamera`

The three features must cooperate without changing the authoritative DFBHD simulation rate.

The intended result is:

- Raw mouse reports can arrive at the physical device rate.
- The original gameplay input path continues to consume mouse movement at the original logic cadence, approximately 62.5 Hz.
- A second mouse stream is sampled once per rendered frame.
- Local first-person mouse-driven camera yaw and pitch can update at render frequency.
- Camera translation, the first-person viewmodel, and supported moving world actors can be rendered from a coherent visual timeline.
- Physics, collision, hitboxes, AI, networking, weapons, timers, gameplay orientation, actor pools, and authoritative transforms remain native.

The design is intentionally render-only wherever possible.

### Source specifications merged by this handoff

This document consolidates the supplied specifications:

```text
high-frequency-visual-camera-implementation-guide(1).md
DFBHD_free_rate_pollmouseinput_tech_summary(1).md
CameraIdea(1).md
```

Where the source documents cover separate experimental stages, this handoff preserves their individual invariants and adds only the integration rules required to run the three features together. The most important added integration rule is that local first-person yaw/pitch prediction and delayed previous/current angular interpolation must not both be applied to the same camera axes in the same frame.

---

## 1. Combined architecture

The three patches solve different parts of the same problem.

```text
physical mouse / WM_INPUT
        |
        | accepted Raw Input packets
        v
+-------------------------------+
| raw_input                     |
|                               |
| logic accumulator             |
| render accumulator            |
| cumulative total raw counts   |
| logic-committed raw counts    |
+-------------------------------+
        |                 |
        |                 |
        v                 v
logic PollMouseInput      render PollMouseInput
~62.5 Hz                  render FPS
        |                 |
        |                 +-----------------------+
        |                                         |
        v                                         v
native DFBHD input                         residual raw counts
processing                                total - committed
        |                                         |
        v                                         v
authoritative player                HighRateCameraRotation
orientation                         temporary prediction only
        |                                         |
        +----------------------+------------------+
                               |
                               v
                     current authoritative root
                     + render-only prediction
                               |
                               v
                    local first-person visual root

DFBHD fixed-step actor producers
        |
        v
previous/current authoritative snapshots
        |
DFBHD scheduler remainder/phase
        |
        v
HighFrequencyVisualCamera / VisualInterpolation
        |
        +--> remote/support actors: interpolate full root
        |
        +--> local FP owner position: interpolate translation
        |
        +--> local FP owner yaw/pitch: use high-rate prediction
        |
        v
render-only camera / viewmodel / world matrix copies
```

The central rule is:

> The DLL may create a higher-frequency visual representation, but it must not advance the authoritative game simulation more often.

---

## 2. The critical integration rule

The original `HighFrequencyVisualCamera` design interpolates both position and angles between the previous and current authoritative actor roots.

The `HighRateCameraRotation` design instead predicts the local first-person yaw and pitch ahead of the next logic tick from Raw Input that has arrived but has not yet been committed to the official DFBHD input stream.

Those two methods must not both independently correct local first-person yaw and pitch.

If they do, the following failure can occur at a logic boundary:

```text
before logic tick:
    official yaw = A
    predicted residual mouse = B
    rendered yaw ~= A + B

next logic tick:
    official yaw becomes ~= A + B
    residual becomes ~= 0

if angular interpolation is also applied:
    interpolated root may still be near A
    rendered camera is pulled back toward A
```

That produces the exact kind of small periodic correction or snap that the predictor is designed to avoid.

Therefore, when all three features are enabled:

- local first-person camera X/Y/Z root motion may use scheduler-derived interpolation;
- local first-person mouse-driven yaw and pitch must use `HighRateCameraRotation`, not previous/current angular interpolation;
- the first-person viewmodel must use the same prediction-aware local visual root;
- supported remote actors continue to use normal position and angular interpolation;
- if the local player model is rendered through a supported world-actor path, it should use the same prediction-aware local visual root rather than the normal delayed angular interpolation path.

This is the main reconciliation required to make the three source designs operate as one system.

---

## 3. Feature ownership

### 3.1 RawMouseInput

Responsibilities:

- own the existing `PollMouseInput` detour at `0x005678F0`;
- receive accepted relative `WM_INPUT` packets;
- maintain independent logic and render accumulators;
- maintain cumulative Raw Input accounting used by the camera predictor;
- own focus/reset handling for all Raw Input counters;
- expose stable read-only APIs to the other modules.

It must not know how camera interpolation or actor rendering works.

### 3.2 FreeRateMousePoll

Responsibilities:

- own the render call-site patch at `0x004B7297`;
- call `raw_input::PollForRenderFrame()` once per rendered camera frame;
- call the original `CalculateCameraPositions()` at `0x0043B5E0`;
- invoke `HighRateCameraRotation` after the native camera calculation;
- remain the single owner of `0x004B7297`.

No other feature should patch this call site.

### 3.3 HighRateCameraRotation

Responsibilities:

- own no independent code hook at `0x004B7297`;
- calculate the mouse movement that has arrived but has not yet been consumed by the authoritative logic input stream;
- reproduce the relevant DFBHD mouse-look pipeline on temporary state;
- derive a render-only yaw/pitch correction;
- apply that correction only to the final render camera yaw/pitch globals;
- publish the same per-frame predicted root-angle delta for the viewmodel and local visual-root code.

It must never write predicted values into the real player orientation or control state.

### 3.4 HighFrequencyVisualCamera / VisualInterpolation

Responsibilities:

- capture the native scheduler visual phase;
- capture authoritative actor roots after their verified producer callbacks;
- keep previous/current snapshots independent from game memory;
- interpolate supported transforms according to their real producer cadence;
- produce render-only root corrections;
- apply corrections only to camera copies, viewmodel matrices, and cloned world-model matrices;
- consume the published high-rate yaw/pitch prediction for the local first-person owner when available.

### 3.5 Existing camera/FOV module

If the DLL already detours the camera builder at `0x004181A0`, that module should remain the single owner of the camera-builder detour.

Visual interpolation should be called from that existing hook rather than installing a second detour on the same function.

---

## 4. Supported executable and fail-closed policy

The visual-interpolation source guide identifies its verified DFBHD target as:

```text
SHA-256:   693676b5fb96012d32ee395ac23b4a8bf4d7dd9a9f25ae8e500749a2f0df34ea
Image base: 0x00400000
Format:     32-bit PE/i386
```

Treat every absolute address in this document as executable-specific.

The FreeRateMousePoll and HighRateCameraRotation addresses must also be validated against the actual supported executable before patching. Do not assume that an address is valid only because another patch in the same module installed successfully.

Every installation step must:

1. verify the expected bytes or an equally strong executable-specific signature;
2. refuse to patch if validation fails;
3. leave the original DFBHD path operational;
4. keep the feature disabled until all hooks required by that feature have installed successfully.

For multi-hook features, prefer transactional installation or explicit rollback.

---

## 5. Important DFBHD addresses

### 5.1 Raw Input and render-camera path

| Purpose | Address | Notes |
|---|---:|---|
| `PollMouseInput` | `0x005678F0` | already detoured by RawMouseInput |
| logic/input update `sub_4B4970` | `0x004B4970` | original logic path |
| logic `PollMouseInput` call site | `0x004B49CB` | keep untouched |
| `CalculateCameraPositions` | `0x0043B5E0` | call original directly from wrapper |
| render camera call site | `0x004B7297` | single owner: FreeRateMousePoll |
| final camera mode | `0x007F2DD0` | guard first-person support |
| final camera target/owner | `0x007F2DD4` | validate local-player ownership |
| neighboring final camera field | `0x007F2DD8` | HighRateCameraRotation must not modify |
| neighboring final camera field | `0x007F2DDC` | HighRateCameraRotation must not modify |
| neighboring final camera field | `0x007F2DE0` | HighRateCameraRotation must not modify |
| final camera yaw | `0x007F2DE4` | predictor may modify in supported path |
| final camera pitch | `0x007F2DE8` | predictor may modify in supported path |
| neighboring final camera field | `0x007F2DEC` | predictor must not modify |
| local player pointer | `0x0096C290` | validate before use |
| game relative mouse X | `0x00F655EC` | logic/render poll output |
| game relative mouse Y | `0x00F655F0` | logic/render poll output |

The original bytes reported for the render call site are:

```text
0x004B7297: E8 44 43 F8 FF
```

which calls `0x0043B5E0`.

### 5.2 Visual interpolation hooks

| Purpose | VA | Overwrite |
|---|---:|---:|
| coherent visual-clock capture | `0x004BF08F` | 5 bytes |
| fast OITEM post-update capture | `0x004803EF` | 5 bytes |
| MITEM post-update capture | `0x00472C2C` | 5 bytes |
| slow OITEM post-update capture | `0x00472E63` | 5 bytes |
| actor render context | `0x00521D80` | 7 bytes |
| model submission A | `0x004FFB20` | 5 bytes |
| model submission B | `0x004FFBA0` | 8 bytes |
| first-person matrix-builder call | `0x00488AF6` | 5 bytes |
| main camera-builder call | `0x004B751A` | call-site identification |
| camera builder | `0x004181A0` | existing camera/FOV detour owner |

Expected signatures from the visual interpolation guide:

```text
0x004BF08F: A1 50 22 A0 00
0x004803EF: A1 10 3F 68 00
0x00472C2C: A1 DC DD 70 00
0x00472E63: A1 10 3F 68 00
0x00521D80: 55 8B EC 53 8B 5D 08
0x004FFB20: 55 8B EC 51 56
0x004FFBA0: 55 8B EC A1 04 B5 64 00
0x00488AF6: E8 65 50 0D 00
```

### 5.3 Visual scheduler and actor state

```text
OITEM pool pointer:       0x00715900
MITEM pool pointer:       0x00715904
Gameplay tick:            0x009F374C
Pause/loading state:      0x007C72BC
Scheduler phase:          0x00A0339C
Scheduler special mode:   0x00A033C0
Global FP transform:      0x0096C440, read only
Actor slot size:          0x29C
OITEM capacity:           256
MITEM capacity:           1200
```

### 5.4 Mouse-look prediction state

The prediction source identifies these relevant locations and functions. Some are explicitly approximate and must be verified before final C++ structure definitions are committed.

```text
first input stage:              sub_4B4970   0x004B4970
mouse processing/bindings:      sub_45FDF0   0x0045FDF0
local player action callback:   sub_435120   0x00435120
player look processing:         sub_438150   0x00438150
orientation-step application:   sub_433210   0x00433210
fixed-point matrix rotation:     sub_55FE00   0x0055FE00
matrix-to-pose conversion:       sub_55E450   0x0055E450
zoom divisor helper:            sub_488260   0x00488260
UpdateWorldCamera:              0x0043BD80, do not call at render rate

input divisor:                  0x00A02248
previous first-stage X:         0x009F3750
previous first-stage Y:         0x009FB280
Y inversion:                    0x009F20E0
mouse sensitivity/scale:        0x009F20E4
```

---

## 6. Authoritative versus visual state

The DLL must maintain a strict boundary between authoritative DFBHD state and render-only state.

### 6.1 Authoritative state that must remain native

Do not write interpolation or prediction into:

```text
actor position/orientation
actor matrix storage
actor pools
player official yaw/pitch/roll
player control values
yaw filter state
physics/collision state
hitboxes
network snapshots
weapon state
timers
AI state
animation state
recoil state
global first-person authoritative transform
```

For the camera predictor in particular, do not write prediction into:

```text
player + 20   yaw
player + 24   pitch
player + 28   roll
player + 120  yaw control
player + 124  pitch control
player + 128  roll control
player + 136  orientation matrix
player + 432  yaw filter state
```

The offsets above come from the supplied reverse-engineering notes and must be verified against the target executable before use.

### 6.2 Allowed render-only modifications

Depending on the feature:

- final camera yaw/pitch globals for the current render frame;
- a stack-local camera source copy;
- a stack-local first-person viewmodel matrix;
- DLL-owned copies of final world-model matrices;
- DLL-owned snapshot and prediction state.

If a prerequisite is not valid for the current frame or object, use the native game data unchanged.

---

## 7. Raw Input state model

Use independent pending streams plus cumulative accounting.

```cpp
struct PredictionSnapshot
{
    uint32_t totalX;
    uint32_t totalY;
    uint32_t committedX;
    uint32_t committedY;
    uint32_t logicPollSerial;
};

volatile LONG g_logicAccumX;
volatile LONG g_logicAccumY;
volatile LONG g_renderAccumX;
volatile LONG g_renderAccumY;

volatile LONG g_lastRenderDeltaX;
volatile LONG g_lastRenderDeltaY;

volatile LONG g_totalRawX;
volatile LONG g_totalRawY;
volatile LONG g_logicCommittedRawX;
volatile LONG g_logicCommittedRawY;
volatile LONG g_logicPollSerial;

volatile LONG g_logicPollCount;
volatile LONG g_renderPollCount;
```

Use interlocked operations for counters shared between the `WM_INPUT` path and poll/read paths.

The cumulative counters intentionally rely on normal 32-bit modular arithmetic. Residual movement is:

```cpp
uint32_t residualX = totalX - committedX;
uint32_t residualY = totalY - committedY;
```

Interpret the wrapped difference using the same signed/unsigned semantics selected by the predictor implementation.

---

## 8. Accepted WM_INPUT packet handling

For every relative Raw Input packet that the current backend actually accepts:

```cpp
InterlockedExchangeAdd(&g_logicAccumX, dx);
InterlockedExchangeAdd(&g_logicAccumY, dy);

InterlockedExchangeAdd(&g_renderAccumX, dx);
InterlockedExchangeAdd(&g_renderAccumY, dy);

InterlockedExchangeAdd(&g_totalRawX, dx);
InterlockedExchangeAdd(&g_totalRawY, dy);

UpdateVirtualCursor(dx, dy);
```

Do not add a movement packet to cumulative totals if the existing input path intentionally discards that packet during focus recovery, anti-jump handling, recenter protection, or another reset transition.

The cumulative totals must represent exactly the physical movement that entered the accepted gameplay Raw Input path.

---

## 9. Logic and render PollMouseInput contexts

Keep the original logic call at `0x004B49CB` untouched.

Use thread-local render-poll context so the existing detour at `0x005678F0` knows which accumulator to drain.

```cpp
thread_local bool g_renderPollContext = false;
```

Prefer a scoped RAII helper internally.

### 9.1 Logic-context poll

The logic call must:

1. drain only `g_logicAccumX/Y`;
2. write the drained values to the normal game relative X/Y fields exactly as before;
3. add the drained values to `g_logicCommittedRawX/Y`;
4. increment `g_logicPollSerial`;
5. increment the logic poll diagnostic counter;
6. continue through the existing native gameplay input pipeline.

The logic path remains authoritative.

### 9.2 Render-context poll

The render call must:

1. run only when Raw Input is fully active;
2. drain only `g_renderAccumX/Y`;
3. update `g_lastRenderDeltaX/Y`;
4. increment the render poll diagnostic counter;
5. never change `g_logicCommittedRawX/Y`;
6. never execute the legacy cursor-based fallback at render FPS.

The render poll must never steal movement from the logic accumulator.

---

## 10. Single render-camera wrapper

`free_rate_mouse_poll.cpp` remains the sole owner of the CALL rewrite at `0x004B7297`.

Do not add a second hook there for camera prediction.

Conceptually:

```cpp
extern "C" int __cdecl CalculateCameraPositions_RenderWrapper()
{
    raw_input::PollForRenderFrame();

    const int result = OriginalCalculateCameraPositions();

    high_rate_camera_rotation::EvaluateAndApplyForCurrentFrame();

    return result;
}
```

Required ordering:

```text
1. render PollMouseInput
2. original CalculateCameraPositions
3. HighRateCameraRotation prediction and camera yaw/pitch application
4. later camera copy / builder / model rendering consumes the result
```

`HighFrequencyVisualCamera` must not install another hook at `0x004B7297`.

If testing shows that render-context writes to `0x00F655EC/F0` influence unrelated render code, use the save/restore isolation option already described by the FreeRateMousePoll patch. The predictor itself should use cumulative accounting, not depend on those globals remaining modified.

---

## 11. High-rate camera prediction

### 11.1 Supported initial path

Only predict when all of the following are true:

```text
HighRateCameraRotation enabled
RawMouseInput enabled and backend active
FreeRateMousePoll enabled and installed
camera mode is verified normal first person
camera target is valid
camera target is the local player
local player pointer is valid
required prediction state is supported
```

Initially leave native behavior for:

- vehicles;
- turrets;
- spectator cameras;
- death cameras;
- third person;
- menus;
- unusual camera modes;
- unsupported pitch-mode branches;
- any state whose exact reverse-engineered behavior is not yet proven.

### 11.2 Why last render delta is not sufficient

Do not build the predictor from only `GetLastRenderDelta()` plus a manually accumulated camera offset.

The logic and render accumulators consume the same physical packet stream independently. At a logic boundary, a packet can already be officially consumed while still having contributed to a render-side accumulation history.

Instead compute:

```text
uncommitted physical movement
    = cumulative accepted Raw Input
    - cumulative movement committed by logic PollMouseInput
```

That residual automatically approaches zero when the next logic tick officially consumes the movement.

### 11.3 Prediction snapshot

Capture the real state needed by the predictor once per evaluation and do not mutate it.

At minimum include:

- cumulative raw and committed totals;
- first-stage divisor and previous X/Y state;
- Y inversion state;
- sensitivity/scale state;
- zoom/scoped state needed by the supported branch;
- current mouse binding information needed to identify look actions;
- current real yaw-filter state;
- current pitch-mode state for the supported on-foot branch;
- current official player orientation matrix;
- any MouseScalingFix fractional state required to reproduce scaling.

### 11.4 Two-branch evaluation

Run two pure temporary simulations from exactly the same captured real state:

```text
zero branch:
    rawDx = 0
    rawDy = 0

moved branch:
    rawDx = residualRawX
    rawDy = residualRawY
```

The correction is the difference between the final moved and zero results.

This cancels state evolution that would happen even without new physical mouse movement.

### 11.5 First input stage

Reproduce the DFBHD integer path described by the source:

```text
currentX = 4 * rawX / dword_A02248
currentY = 4 * rawY / dword_A02248

mouseX = currentX + dword_9F3750
mouseY = currentY + dword_9FB280
```

The real logic later stores `currentX/currentY` back to its globals. The predictor must not.

Use local copies only.

### 11.6 Sensitivity, inversion, zoom, and binding selection

Reproduce the relevant supported portion of `sub_45FDF0` using integer semantics.

Base scale is approximately:

```text
scale = dword_9F20E4 << 11
```

Then apply the game's Y inversion convention and fixed-point scaling.

For zoomed/scoped paths, preserve the effective scale change identified through the supplied reverse-engineering path, including the divisor returned by `sub_488260` when that path is active.

Do not call the real action-dispatch path from the render loop.

Capture only look actions into a local result.

Action mapping from the source notes:

```text
259 -> positive pitch
260 -> negative pitch
263 -> positive yaw
264 -> negative yaw
```

Production code should honor the current mouse bindings rather than assuming permanent X-to-yaw and Y-to-pitch mapping.

### 11.7 MouseScalingFix interaction

If `MouseScalingFix` maintains fractional remainder state, expose a pure simulation API that can run against a caller-supplied copy of that state.

Requirements:

- zero and moved branches receive independent copies of the same starting state;
- the predictor never mutates the real gameplay remainder state;
- the calculation used for prediction matches the gameplay patch exactly.

### 11.8 Yaw control

The supplied reverse-engineering notes describe the relevant yaw filter approximately as:

```text
sum = yawInput + yawFilterState
filtered = sum - ((sum + 4) >> 3)
yawControl = 3072 * filtered
```

Preserve signed 16-bit truncation where the game uses it.

Do not advance the real yaw filter once per render frame.

### 11.9 Pitch control

For the initial normal on-foot path, reproduce the verified supported branch.

The source notes identify the base operation as:

```text
pitchControl = pitchInput << 14
```

and one observed normal path further applies:

```text
pitchControl = (135168 * pitchControl) >> 16
```

If the live player enters a pitch-control branch that has not been proven, skip prediction for that frame instead of guessing.

### 11.10 Control-to-step conversion

Preserve the game's rounding independently in both branches:

```text
step = (control + 2) >> 2
```

Do not subtract the controls first and round only once.

Calculate:

```text
zeroYawStep
movedYawStep
zeroPitchStep
movedPitchStep
```

separately.

### 11.11 Temporary orientation matrices

Copy the real player orientation matrix into two temporary buffers:

```text
zeroMatrix
movedMatrix
```

Never pass the real matrix to a mutating function from the predictor.

Apply zero-branch steps to `zeroMatrix` and moved-branch steps to `movedMatrix` using the exact DFBHD fixed-point rotation behavior.

The supplied notes identify:

```text
sub_55FE00 at 0x0055FE00  fixed-point rotation-matrix operation
sub_55E450 at 0x0055E450  matrix-to-pose conversion
```

Calling the original game helpers is acceptable only after their ABI, structures, and mutation behavior have been verified, and only with temporary buffers.

Otherwise reproduce their exact fixed-point math in the DLL.

### 11.12 Final prediction result

Convert both temporary matrices to poses and calculate wrap-safe angular differences:

```cpp
struct VisualLookPrediction
{
    bool valid;
    uint32_t frameSerial;
    int32_t yawDelta;
    int32_t pitchDelta;
};
```

Conceptually:

```text
visualYawDelta   = movedPose.yaw   - zeroPose.yaw
visualPitchDelta = movedPose.pitch - zeroPose.pitch
```

Use wrap-safe 32-bit angle subtraction.

Apply the result only to:

```text
0x007F2DE4  final render camera yaw
0x007F2DE8  final render camera pitch
```

Publish the same `VisualLookPrediction` for later render-only consumers during the same frame.

Do not maintain a long-lived accumulated render yaw/pitch offset as predictor authority.

Recompute from the current official state and current uncommitted Raw Input every frame.

---

## 12. Visual interpolation clock

The visual interpolation system should use the native DFBHD scheduler rather than wall-clock camera sampling.

At `0x004BF08F`, capture as one coherent observation:

```text
EBX                   post-catch-up remainder
[0x00A0339C]          post-increment substep phase
[0x007C72BC]          pause/loading state
[0x00A033C0]          special scheduler mode
```

The scheduler representation from the supplied guide is:

```text
1 scheduler unit = 1/16 ms
4 ms step         = 0x40 units
normal remainder  = 0..0x3F
```

Suggested state:

```cpp
struct VisualClock
{
    uint32_t remainder;
    uint32_t phase;
    bool valid;
};
```

Clock validity:

```cpp
valid = remainder < 0x40 &&
        pauseState == 0 &&
        specialMode == 0;
```

Invalidate interpolation history when pause/special state changes or world/tick continuity is reset.

The inline hook must preserve every GPR and EFLAGS, especially `EBX`.

---

## 13. Producer snapshots

Authoritative actor root format:

```cpp
struct TransformQ16
{
    int32_t x;
    int32_t y;
    int32_t z;
    uint32_t yaw;
    uint32_t pitch;
    uint32_t roll;
};
```

Known offsets within the actor root:

```text
+0x08 x
+0x0C y
+0x10 z
+0x14 yaw
+0x18 pitch
+0x1C roll
```

Suggested tracking state:

```cpp
enum class Cadence
{
    Unknown,
    Hz250,
    Hz62_5
};

struct Snapshot
{
    TransformQ16 transform;
    uint32_t producerStep;
    bool valid;
};

struct Track
{
    Snapshot previous;
    Snapshot current;
    Cadence cadence;

    uint32_t definitionIndex;
    uintptr_t definition;
    uint32_t logicalId;
    uintptr_t rideTarget;
    uint32_t externalGeneration;
};
```

Capture after the verified producer has finished updating the actor:

```text
0x004803EF fast OITEM, treated as 250 Hz
0x00472E63 slow OITEM, treated as 62.5 Hz
0x00472C2C MITEM, treated as 62.5 Hz
```

Revalidate the actor after callbacks because a callback may destroy or replace the slot.

Advance the producer timestamp even when the transform did not numerically change.

Do not advance the same track twice for the same producer step.

---

## 14. Actor identity and rebase rules

Map a pointer to a known pool/slot only after proving:

- pointer is inside the pool;
- pointer is aligned to the `0x29C` stride;
- slot is below the verified pool capacity.

Useful identity/lifecycle fields from the supplied guide:

```text
actor + 0x00 definition index / active indicator
actor + 0x24 definition pointer
actor + 0x4C logical ID when available
actor + 0x13C ride/mount target
actor + 0x224 slow-update callback
actor + 0x228 fast movement callback
```

Rebase `previous = current = live` when:

- a slot becomes active;
- definition index or pointer changes;
- a valid logical ID changes;
- ride target changes;
- producer cadence changes;
- movement exceeds the teleport threshold;
- pause/special scheduler state changes;
- world/tick continuity is reset;
- live authoritative transform differs from the stored current snapshot without a verified producer capture.

The supplied implementation uses a per-axis teleport threshold of:

```text
64 world units * 65536
```

Unknown or externally modified state should fall back to native rendering rather than interpolating stale history.

---

## 15. Interpolation alpha

Use Q16 alpha where:

```text
0      = 0.0
65536  = 1.0
```

### 15.1 250 Hz producer

```cpp
alpha250 = remainder * 65536 / 64;
```

### 15.2 62.5 Hz producer

The supplied scheduler mapping is:

```cpp
completedQuarters = (postIncrementPhase - 1) & 3;
units = completedQuarters * 64 + remainder;
alpha62 = units * 65536 / 256;
```

With zero remainder:

```text
phase 1 -> 0.00
phase 2 -> 0.25
phase 3 -> 0.50
phase 0 -> 0.75
```

This is interpolation between previous and current authoritative snapshots. It can add up to one authoritative update of visual latency on 62.5 Hz producers.

That latency is acceptable for world-motion smoothing, but local mouse yaw/pitch should use the high-rate prediction path when that feature is active.

---

## 16. Interpolation math

Position interpolation should preserve signed Q16 arithmetic with 64-bit intermediates.

```cpp
int32_t LerpQ16(int32_t from, int32_t to, uint32_t alphaQ16)
{
    const int64_t delta = int64_t(to) - from;
    return int32_t(from + delta * alphaQ16 / 65536);
}
```

Full-turn angles must interpolate through the shortest modular path.

```cpp
uint32_t LerpAngle(uint32_t from, uint32_t to, uint32_t alphaQ16)
{
    const uint32_t modular = to - from;
    const int64_t shortest = modular <= 0x7fffffff
        ? int64_t(modular)
        : int64_t(modular) - 0x100000000LL;

    return from + uint32_t(shortest * alphaQ16 / 65536);
}
```

Do not rely on signed 32-bit overflow for the modular delta.

---

## 17. The prediction-aware local visual root

This is the integration object that lets the three features cooperate.

Define a helper conceptually similar to:

```cpp
struct LocalVisualRoot
{
    TransformQ16 transform;
    bool valid;
    bool predictedYawPitch;
};

bool BuildLocalVisualRoot(Actor* localPlayer,
                          LocalVisualRoot* out);
```

### 17.1 When HighRateCameraRotation is not active

Use the normal full interpolated root:

```text
position = Lerp(previous.position, current.position, alpha)
yaw      = LerpAngle(previous.yaw, current.yaw, alpha)
pitch    = LerpAngle(previous.pitch, current.pitch, alpha)
roll     = LerpAngle(previous.roll, current.roll, alpha)
```

### 17.2 When HighRateCameraRotation is active and valid

Use a hybrid visual root:

```text
position = interpolated authoritative position

yaw      = current authoritative yaw
           + published high-rate visual yaw delta

pitch    = current authoritative pitch
           + published high-rate visual pitch delta

roll     = current authoritative roll for the initial implementation
```

Do not add the previous/current interpolation angle delta to yaw or pitch in this mode.

The prediction delta is already derived from the current official state plus the Raw Input not yet committed to the next logic update.

Using the delayed angular interpolation at the same time would pull the rendered local orientation backwards at logic boundaries.

### 17.3 Frame validity

The published prediction must be tied to the current render frame, for example with a monotonically increasing render-frame serial.

A consumer must not reuse a previous frame's prediction after:

- focus loss;
- Raw Input reset;
- map transition;
- camera mode change;
- local-player change;
- predictor invalidation;
- pause/special mode transition.

If the prediction is stale or invalid, fall back to the normal interpolation/native rule selected for that feature configuration.

---

## 18. Camera integration

### 18.1 Native camera builder boundary

The supplied visual-camera guide identifies a `0x40`-byte stack-local camera source passed to the main camera builder.

Relevant source fields:

```text
+0x04/+0x08/+0x0C  signed-Q16 position
+0x10/+0x14/+0x18  full-turn angles
+0x3C              horizontal FOV
```

Correct only the verified normal first-person path and the verified main call.

Copy the entire `0x40` bytes before modification.

### 18.2 Why the camera source is not replaced

The native camera source can already contain current-frame effects such as:

- eye height;
- bob;
- recoil;
- shake;
- lean;
- vehicle offsets;
- other camera-specific offsets.

Do not replace it with the actor root.

Apply only root-derived deltas to the copy.

### 18.3 Camera behavior when prediction is disabled

Normal visual interpolation can apply:

```text
positionDelta = interpolatedOwner.position - currentOwner.position
angleDelta    = interpolatedOwner.angle    - currentOwner.angle

stackCamera.position += positionDelta
stackCamera.angle    += angleDelta
```

using wrap-safe angle arithmetic.

### 18.4 Camera behavior when HighRateCameraRotation is active

`HighRateCameraRotation` has already added the high-rate mouse prediction to the final render camera yaw/pitch before the renderer prepares the stack-local camera source.

Therefore the camera interpolation hook must not add the prediction again.

For the initial combined implementation:

```text
apply interpolated owner position delta to camera X/Y/Z
leave predicted camera yaw untouched
leave predicted camera pitch untouched
leave camera roll native
```

In other words, camera translation remains on the coherent interpolation timeline while local mouse yaw/pitch remain on the high-rate prediction timeline.

If a future implementation wants to interpolate a non-mouse root roll component, add it only after proving the exact interaction with the camera's native roll behavior.

---

## 19. First-person viewmodel integration

The supplied visual-interpolation path lets the game build its normal first-person transform, then intercepts the native matrix-builder call at `0x00488AF6`.

The global first-person transform at `0x0096C440` remains read-only.

The game should first build the normal native matrix.

Then apply a render-only root correction.

### 19.1 Matrix convention

The supplied guide identifies final model matrices as:

```text
0x40-byte row-major float[4][4]
row-vector composition
translation in fourth row
```

Translation fields:

```text
m[3][0] = Tx
m[3][1] = Ty
m[3][2] = Tz
m[3][3] = 1
```

World-to-render position mapping used by the native transform builder at `0x0055DB60`:

```text
renderX = worldX / 65536.0
renderY = worldZ / 65536.0
renderZ = -worldY / 65536.0
```

### 19.2 Root correction

For the verified row-vector convention:

```text
NativeFinal = Local * CurrentRoot
WantedFinal = Local * VisualRoot

Correction = Inverse(CurrentRoot) * VisualRoot
WantedFinal = NativeFinal * Correction
```

The correction is right-multiplied.

### 19.3 Prediction-aware viewmodel root

When HighRateCameraRotation is valid for the local first-person owner, `VisualRoot` must be the prediction-aware local visual root from section 17:

```text
interpolated position
current yaw   + predicted yaw delta
current pitch + predicted pitch delta
current roll
```

This prevents the weapon/viewmodel from remaining on delayed actor angles while the camera turns at render frequency.

Do not simply use the ordinary previous/current interpolated yaw/pitch for the viewmodel when the camera is using prediction.

Applying the correction after the native matrix is built preserves the native local weapon placement, ADS, recoil, sway, inertia, and animation as much as the existing render-only architecture allows.

---

## 20. World actor integration

### 20.1 Render context

Detour the verified actor render dispatcher and maintain a thread-local actor stack.

Use a stack rather than one global current-actor pointer because render calls may be nested.

Do not use `Model*` as actor identity because one model can be shared by many actors.

### 20.2 Final matrix submission

The verified submitters receive:

```cpp
void SubmitModel(Model* model, Matrix4x4* matrices);
```

The supplied implementation accepts matrix counts only in the verified range `1..51`.

Clone final matrices into DLL-owned temporary storage, apply the root correction to every final palette matrix, then pass the clone to the original submitter.

Never overwrite actor-owned matrix storage.

The temporary buffer must remain valid until the original submission returns. This depends on synchronous pointer consumption and must remain a verified assumption.

### 20.3 Remote and normal supported actors

For remote/supported world actors, use the normal interpolated root:

```text
previous -> current using cadence-specific alpha
```

including shortest-path angular interpolation.

### 20.4 Local player actor while in first person

If the local player actor is actually submitted through one of the supported world paths while normal first-person prediction is active, use the same prediction-aware local visual root used by the viewmodel.

This keeps any visible local geometry on the same render-only yaw/pitch as the camera and weapon.

If the local actor path is not proven, leave it native rather than guessing.

---

## 21. Root correction safety

Use a guarded affine inverse.

Reject and submit native data when:

- matrix is non-affine;
- matrix is singular;
- matrix contains non-finite values;
- model matrix count is outside the verified range;
- actor context is absent;
- actor identity is unstable;
- snapshot history is invalid;
- producer cadence is unknown;
- visual clock is invalid;
- live transform differs from the current snapshot because of an unobserved authoritative writer.

Apply the correction to every final matrix when the palette matrices are already in actor/world space.

Correcting only matrix zero can leave bones or child attachments on the authoritative root while the root itself is visually moved.

---

## 22. Combined per-frame sequence

A normal supported frame should conceptually behave as follows.

```text
A. authoritative fixed-step catch-up
    |
    +--> normal gameplay PollMouseInput at its native cadence
    |       |
    |       +--> logic accumulator drained
    |       +--> committed Raw Input totals advance
    |       +--> native input/action/player orientation update
    |
    +--> actor producer callbacks update authoritative roots
            |
            +--> capture previous/current snapshots after producers

B. post-catch-up visual clock capture
    |
    +--> capture scheduler remainder/phase as one coherent observation

C. render camera boundary at 0x004B7297
    |
    +--> raw_input::PollForRenderFrame()
    |       +--> drain render accumulator only
    |
    +--> Original CalculateCameraPositions()
    |
    +--> HighRateCameraRotation::EvaluateAndApplyForCurrentFrame()
            |
            +--> residual = totalRaw - committedRaw
            +--> zero and moved prediction branches
            +--> add yaw/pitch delta to final render camera globals
            +--> publish VisualLookPrediction for this frame

D. camera builder
    |
    +--> clone native camera source
    +--> normal FP + prediction valid:
    |       apply interpolated root position delta only
    |       do not re-interpolate yaw/pitch
    |
    +--> otherwise:
            apply normal supported interpolation delta

E. first-person viewmodel
    |
    +--> game builds native matrix
    +--> build prediction-aware local VisualRoot when valid
    +--> right-multiply root correction into stack-local matrix

F. world actors
    |
    +--> identify current actor through render-context stack
    +--> clone final palette
    +--> remote actors use normal interpolated VisualRoot
    +--> verified local FP actor uses prediction-aware VisualRoot
    +--> submit clone synchronously
```

---

## 23. Reset and invalidation rules

All dependent state must be invalidated coherently.

### 23.1 Raw Input reset

Clear together:

```text
logic accumulators
render accumulators
last render deltas
cumulative total raw counts
logic-committed raw counts
logic poll serial
button/input state already cleared by the backend
published VisualLookPrediction
```

Use the same reset transitions already handled by RawMouseInput, including:

- focus loss;
- focus recovery;
- suspend;
- destruction;
- backend re-registration/recovery;
- map/world transition when applicable.

No stale movement may survive a reset.

### 23.2 Visual interpolation reset

Rebase or invalidate histories on:

- pause/special scheduler state changes;
- map/world transition;
- tick continuity reset;
- entity slot reuse;
- definition or logical identity change;
- ride/mount target change;
- cadence change;
- teleport threshold exceedance;
- unobserved authoritative transform change.

### 23.3 Camera prediction invalidation

Invalidate the per-frame prediction when:

- camera mode becomes unsupported;
- camera owner is not the local player;
- Raw Input becomes inactive;
- FreeRateMousePoll is unavailable;
- required mouse-look state is unsupported;
- frame serial changes without a fresh prediction;
- input/reset state changes.

---

## 24. Hook ownership table

| Hook / address | Single owner | Consumers |
|---|---|---|
| `0x005678F0` PollMouseInput detour | `raw_input` | logic and render poll contexts |
| `0x004B7297` render camera CALL | `free_rate_mouse_poll` | Raw Input render poll, original camera, HighRateCameraRotation |
| `0x004BF08F` scheduler capture | `visual_interpolation` | interpolation clock |
| `0x004803EF` fast OITEM capture | `visual_interpolation` | actor snapshots |
| `0x00472E63` slow OITEM capture | `visual_interpolation` | actor snapshots |
| `0x00472C2C` MITEM capture | `visual_interpolation` | actor snapshots |
| `0x00521D80` actor render context | `visual_interpolation` | world submission wrappers |
| `0x004FFB20` model submission A | `visual_interpolation` | cloned palettes |
| `0x004FFBA0` model submission B | `visual_interpolation` | cloned palettes |
| `0x00488AF6` FP matrix-builder call | `visual_interpolation` | viewmodel correction |
| `0x004181A0` camera builder detour | existing `camera_fov` module | FOV and camera visual correction |

The purpose of this ownership model is to prevent multiple modules from rewriting the same entry point or call site in incompatible order.

---

## 25. Suggested module boundaries

```text
src/raw_input.h
src/raw_input.cpp
    existing Raw Input backend
    logic/render accumulators
    cumulative total/committed accounting
    PredictionSnapshot API
    reset handling
    statistics

src/free_rate_mouse_poll.h
src/free_rate_mouse_poll.cpp
    sole owner of 0x004B7297
    PollForRenderFrame -> original CalculateCameraPositions
    -> HighRateCameraRotation Apply sequence

src/high_rate_camera_rotation.h
src/high_rate_camera_rotation.cpp
    guard supported local FP state
    capture prediction snapshot
    calculate residual raw movement
    evaluate zero/moved branches
    apply final render camera yaw/pitch
    publish VisualLookPrediction

src/game_mouse_look_predictor.h
src/game_mouse_look_predictor.cpp
    pure reproduction of the supported DFBHD mouse-look path
    no authoritative writes

src/mouse_scaling_fix.h
src/mouse_scaling_fix.cpp
    existing gameplay fix
    add pure evaluate-with-copied-state API for predictor

src/visual_interpolation.h
src/visual_interpolation.cpp
    visual clock
    actor snapshots
    actor identity/rebase
    interpolation
    root correction
    actor render context
    model submission wrappers
    local prediction-aware VisualRoot helper

src/visual_interpolation_math.h
    transform interpolation
    affine inverse
    matrix multiply
    root-correction helpers

src/visual_camera_math.h
    Q16 and wrapped-angle helpers

src/camera_fov.cpp
    existing camera-builder owner
    stack camera copy
    FOV logic
    position interpolation integration
    preserve predicted yaw/pitch in combined mode

src/dinput8.cpp
    configuration
    dependency validation
    installation order only
```

`dinput8.cpp` should not become the implementation site for the prediction or interpolation algorithms.

---

## 26. Configuration and dependencies

Suggested configuration:

```ini
[PatchGroups]
RawMouseInput=1
FreeRateMousePoll=1
HighRateCameraRotation=1
HighFrequencyVisualCamera=1
```

Dependency rules:

```text
FreeRateMousePoll
    requires RawMouseInput

HighRateCameraRotation
    requires RawMouseInput
    requires FreeRateMousePoll

HighFrequencyVisualCamera
    can operate independently of HighRateCameraRotation
    but uses prediction-aware local yaw/pitch when the predictor is enabled and valid
```

Recommended failure behavior:

```text
RawMouseInput fails
    -> disable FreeRateMousePoll
    -> disable HighRateCameraRotation
    -> HighFrequencyVisualCamera may still run if its own prerequisites are valid

FreeRateMousePoll fails
    -> disable HighRateCameraRotation
    -> HighFrequencyVisualCamera may still run independently

HighRateCameraRotation fails or is unsupported for current frame
    -> do not predict camera yaw/pitch
    -> HighFrequencyVisualCamera may use its normal interpolation behavior

HighFrequencyVisualCamera fails installation
    -> leave camera/viewmodel/world interpolation native
    -> FreeRateMousePoll and HighRateCameraRotation may still operate if valid
```

Keep development options disabled by default until the exact executable and runtime behavior are validated.

---

## 27. Installation order

Recommended implementation order for the complete DLL:

### Phase 1: verify target executable

1. Identify the supported DFBHD build.
2. Verify all required expected bytes/signatures.
3. Log unsupported executable versions and install nothing unsafe.

### Phase 2: FreeRateMousePoll

1. Split logic and render Raw Input accumulators.
2. Feed every accepted packet into both streams.
3. Add render-poll TLS context.
4. Refactor `RawPollMouseInput()` to drain the selected stream.
5. Add separate logic/render statistics.
6. Add `PollForRenderFrame()`.
7. Install the single `0x004B7297` wrapper.
8. Verify mouse movement conservation before proceeding.

### Phase 3: cumulative accounting

1. Add total accepted Raw Input counters.
2. Add logic-committed counters.
3. Add logic poll serial.
4. Expose `PredictionSnapshot`.
5. Reset all counters atomically/coherently with the existing input reset path.
6. Verify `total - committed` behavior around logic boundaries.

### Phase 4: HighRateCameraRotation

1. Implement the pure first input stage.
2. Implement sensitivity/inversion/zoom path for the narrow supported case.
3. Implement look-action capture without real dispatch.
4. Implement pure MouseScalingFix simulation if enabled.
5. Implement yaw filter prediction.
6. Implement supported pitch prediction.
7. Implement control-to-step rounding.
8. Verify temporary orientation-matrix handling.
9. Implement zero/moved branch comparison.
10. Apply only final camera yaw/pitch.
11. Publish the same per-frame `VisualLookPrediction`.
12. Validate no correction snap at the approximately 16 ms logic boundary.

### Phase 5: visual clock and snapshots

1. Install the scheduler capture only.
2. Confirm phase/remainder behavior without modifying rendering.
3. Install producer captures.
4. Verify cadence and actor identity.
5. Add teleport/rebase handling.
6. Add diagnostics before enabling visual corrections.

### Phase 6: combined local camera translation

1. Integrate with the existing camera/FOV hook.
2. Copy the native camera source.
3. With prediction active, apply only interpolated owner position delta.
4. Confirm predicted yaw/pitch are not double-applied or pulled backward.
5. Confirm bob/recoil/shake/lean remain native.

### Phase 7: viewmodel

1. Wrap the verified first-person matrix-builder call.
2. Build the prediction-aware local visual root.
3. Apply the root correction to the stack-local native matrix.
4. Verify weapon alignment during high-rate mouse turns and player movement.

### Phase 8: world actors

1. Install actor render-context tracking.
2. Validate nested calls.
3. Install submission wrappers.
4. Start with translation correction only if needed for diagnosis.
5. Enable full normal angular interpolation for verified remote actors.
6. Use the prediction-aware local root only for a verified local-player render path.
7. Expand actor categories gradually.

---

## 28. Diagnostics

### 28.1 Raw Input diagnostics

Track at least:

```text
raw reports per second
logic polls per second
render polls per second
raw total X/Y
logic-consumed total X/Y
render-consumed total X/Y
logic committed total X/Y
residual raw X/Y
```

At 1000 Hz mouse input and 240 FPS, a healthy result should be approximately:

```text
raw reports     ~1000 Hz
logic polls      ~62.5 Hz
render polls     ~240 Hz
```

Over a sufficiently long interval:

```text
logic consumed total ~= accepted raw total
render consumed total ~= accepted raw total
```

Small boundary differences are acceptable when movement is still pending in an accumulator.

Systematic loss is not.

### 28.2 Predictor diagnostics

At a low logging rate, track:

```text
residual raw X/Y
zero/moved first-stage X/Y
zero/moved scaled X/Y
zero/moved pitch/yaw action values
zero/moved yawControl
zero/moved pitchControl
zero/moved rotation steps
final visual yaw delta
final visual pitch delta
prediction valid/invalid reason
logic poll serial
render frame serial
```

Do not log once per `WM_INPUT` packet during normal use.

### 28.3 Predictor validation mode

Add an optional validation mode at a real logic tick.

Capture the real pre/post state and compare the predictor against the actual logic result for the same raw movement.

Useful comparison points from the supplied notes include:

```text
raw movement consumed by the tick
word_872384 pitch action value
word_87238E yaw action value
player + 120 yaw control
player + 124 pitch control
yaw filter state before/after
player yaw/pitch before/after
predicted temporary pose
```

This is preferable to tuning by visual feel.

### 28.4 Visual interpolation diagnostics

Track at least:

```text
visual clock validity
snapshot captures by cadence
camera position corrections
camera angular interpolation corrections
camera prediction-preserve path count
viewmodel root corrections
world palette corrections
prediction-aware local-root corrections
native fallback: no context
native fallback: invalid matrix count
native fallback: missing snapshot
native fallback: stale prediction
native fallback: unsupported camera mode
unobserved authoritative changes
teleport/rebase events
```

---

## 29. Acceptance tests

### 29.1 FreeRateMousePoll

Pass when:

1. `WM_INPUT` continues to run at the physical report rate.
2. Logic `PollMouseInput()` remains approximately 62.5 Hz.
3. Render `PollMouseInput()` runs approximately once per rendered frame.
4. Render polling never reduces or fragments movement seen by the logic path.
5. Sensitivity and gameplay behavior remain unchanged.
6. Focus loss/recovery creates no stale movement.
7. Legacy cursor polling is never called at render FPS while Raw Input is inactive.
8. Disabling `FreeRateMousePoll` restores the previous DLL behavior.

### 29.2 HighRateCameraRotation

Initial test case:

```text
normal first person
on foot
standing still
not scoped
not in a vehicle
not spectating
not in a menu
144 / 240 / 360 FPS or higher
```

Pass when:

1. visual yaw/pitch update at render frequency;
2. real player yaw/pitch remain logic-rate;
3. there is no small correction approximately every 16 ms;
4. fixed physical mouse sweep ends at the same official orientation as native behavior;
5. total turn distance is unchanged;
6. sensitivity is unchanged;
7. Y inversion agrees with the official path;
8. multiplayer/gameplay state remains unchanged;
9. disabling the predictor restores FreeRateMousePoll-only behavior.

After the base path is exact, verify multiple sensitivities, inverted Y, scoped/zoom behavior, and MouseScalingFix compatibility.

### 29.3 HighFrequencyVisualCamera

Unit-test engine-independent math:

1. 250 Hz phase mapping;
2. 62.5 Hz phase mapping;
3. Q16 position interpolation;
4. full-turn angular wrap in both directions;
5. exact alpha endpoints;
6. affine inverse and singular rejection;
7. row-vector matrix multiplication;
8. right-side root correction for translation;
9. right-side root correction for rotation away from world origin;
10. teleport detection;
11. identity/rebase rules;
12. duplicate producer-step suppression;
13. pause/clock invalidation.

Runtime tests must verify:

- correct hook signatures;
- expected snapshot cadence;
- no interpolation in menus or unsupported camera modes;
- native fallback for unknown actors;
- no actor-pool writes;
- no hitbox, muzzle, weapon, or networking changes;
- stable pause, focus, death, mount, map-change, and teleport behavior;
- safe temporary matrix-buffer lifetime.

### 29.4 Combined-mode acceptance

With all three features enabled, additionally verify:

1. camera movement from player translation is visually smooth;
2. local mouse yaw/pitch remains low-latency and render-rate;
3. camera does not snap backward when the next 62.5 Hz logic tick consumes the predicted mouse movement;
4. the viewmodel remains rigidly aligned with the predicted camera during fast turns;
5. the viewmodel does not visibly lag one logic tick behind the camera;
6. remote actors move and rotate smoothly on the scheduler-derived interpolation timeline;
7. any verified local-player world geometry uses the same prediction-aware yaw/pitch as the local viewmodel;
8. recoil, sway, ADS, head bob, shake, and lean remain native unless explicitly changed by another patch;
9. authoritative orientation, hitboxes, networking, and total turn distance remain unchanged.

---

## 30. Diagnosing common failures

### 30.1 Camera is smooth between logic ticks but snaps every approximately 16 ms

Do not hide the snap with another smoothing filter.

Check in this order:

1. cumulative total and committed Raw Input accounting;
2. reset timing and poll serials;
3. first input stage `4 * raw / divisor`;
4. previous X/Y contribution;
5. Y inversion;
6. sensitivity scaling;
7. zoom scaling;
8. MouseScalingFix copied remainder behavior;
9. current mouse binding/action mapping;
10. signed 16-bit truncation;
11. yaw filter state;
12. pitch branch;
13. `+2 >> 2` step rounding;
14. orientation matrix layout;
15. angle wrapping;
16. whether visual interpolation is incorrectly re-interpolating local yaw/pitch after prediction.

### 30.2 Camera is smooth but weapon visibly trails during mouse turns

Likely causes:

- viewmodel still uses the ordinary delayed interpolated angular root;
- viewmodel correction does not consume the current frame's prediction;
- prediction frame serial is stale;
- viewmodel is rendered before the prediction is published.

Verify render order. If the viewmodel can execute before the prediction is evaluated, refactor prediction calculation so a single pure per-frame evaluation can be produced early enough and then applied to the camera after `CalculateCameraPositions`, without changing the authoritative game state.

Do not calculate two different predictions for camera and viewmodel from different input snapshots.

### 30.3 Camera turns twice as far as expected

Likely cause:

- HighRateCameraRotation applied prediction to final camera globals;
- camera interpolation hook then added the same prediction-aware angular delta again.

In combined mode, the camera builder should preserve predicted yaw/pitch and apply only the intended positional interpolation correction.

### 30.4 Weapon or actor orbits around world origin

Likely cause:

- root correction multiplied on the wrong side;
- matrix convention assumed incorrectly.

For the verified DFBHD row-vector convention:

```text
Correction = Inverse(CurrentRoot) * VisualRoot
WantedFinal = NativeFinal * Correction
```

### 30.5 Logic movement becomes weak or fragmented

Likely cause:

- render poll is draining the logic accumulator;
- both contexts share one accumulator;
- reset races discard logic movement.

Every accepted Raw Input packet must be copied into independent logic and render pending streams.

### 30.6 Remote actors still look stepped

Check:

- producer coverage;
- cadence classification;
- visual-clock capture;
- snapshot advancement on unchanged transforms;
- actor identity/rebase rules;
- render context and model submission coverage.

Do not interpolate unknown actor categories until their producer and submission path are verified.

---

## 31. Non-goals

These patches do not change the authoritative DFBHD simulation frequency.

Do not use them to run any of the following at render FPS:

```text
full player update
UpdateWorldCamera
authoritative physics
collision
weapons
AI
timers
networking
actor producer callbacks
real input action dispatch
real yaw-filter advancement
real orientation-matrix mutation
```

The initial `HighRateCameraRotation` path is not responsible for:

- camera X/Y/Z smoothing;
- head-bob frequency;
- animation sampling frequency;
- recoil translation frequency;
- third-person camera translation;
- vehicle/turret camera prediction;
- spectator/death-camera prediction.

`HighFrequencyVisualCamera` can smooth verified root motion, but it does not create new animation samples. Animation channels that themselves update at a low rate require a separate render-only animation interpolation system.

---

## 32. Final implementation contract

A correct combined implementation should satisfy all of the following:

```text
Raw Input packets
    may arrive at device rate

logic PollMouseInput
    remains native cadence
    remains authoritative
    receives the complete movement stream

render PollMouseInput
    runs once per render frame
    receives an independent copy of the movement stream

HighRateCameraRotation
    predicts only uncommitted Raw Input
    writes only render camera yaw/pitch
    publishes the same per-frame prediction for visual consumers

local first-person camera
    uses interpolated root translation
    uses predicted mouse yaw/pitch
    does not receive delayed angular interpolation on top of prediction

first-person viewmodel
    keeps native local weapon effects
    receives a root correction to the same prediction-aware local visual root

remote/supported world actors
    use scheduler-derived previous/current interpolation

local world actor, if verified and rendered
    uses the same prediction-aware local visual root

authoritative DFBHD state
    stays unchanged
```

The implementation should fail closed. If a hook, state branch, entity identity, matrix layout, prediction input, or render context cannot be proven safe, render the native DFBHD result for that path instead of guessing.

