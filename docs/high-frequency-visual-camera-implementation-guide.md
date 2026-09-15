# High-frequency visual camera — implementation guide

## 1. Purpose

This document describes how `HighFrequencyVisualCamera=1` is implemented in
BHD_QoL and how to reproduce the same architecture in another project.

Despite its historical option name, the feature is not only a camera filter.
It keeps three visual consumers on the same interpolated timeline:

1. the first-person camera;
2. the primary first-person weapon/viewmodel;
3. supported moving world actors, including the verified player, organic-actor,
   and vehicle paths.

The authoritative game simulation remains unchanged. Physics, collision,
hitboxes, AI, networking, audio, weapon logic, and actor pools continue to use
the game's original transforms.

The reusable design is:

```text
authoritative fixed-step update
    -> capture previous/current root transforms

visual-frame boundary
    -> capture one coherent scheduler-derived interpolation phase

camera
    -> add interpolated-root delta to a stack-local camera copy

viewmodel
    -> apply the same root correction to its stack-local native matrix

world actors
    -> clone final matrices
    -> apply the same root correction to the clones

renderer
    -> synchronously consume only render-only copies
```

## 2. Portability warning

The interpolation architecture is reusable, but the addresses, instruction
signatures, ABIs, structure offsets, matrix convention, and hook lengths in
this document are specific to this executable:

```text
SHA-256:   693676b5fb96012d32ee395ac23b4a8bf4d7dd9a9f25ae8e500749a2f0df34ea
Image base: 0x00400000
Format:     32-bit PE/i386
```

Do not copy the addresses into another game or another executable revision.
For a different target, independently recover and verify:

- the fixed-step accumulator and render boundary;
- every authoritative transform producer;
- stable entity identity and lifecycle;
- camera and viewmodel render-only boundaries;
- the actor render dispatcher;
- the final model-submission functions;
- the model matrix-count field and safe bounds;
- the matrix storage and multiplication convention;
- whether matrix pointers are consumed synchronously.

Every patch must validate expected bytes and fail closed when validation fails.

## 3. Behavioral contract

When enabled, the feature may change only data that is private to the current
render operation:

- a stack-local camera source;
- a stack-local first-person matrix;
- DLL-owned copies of final world-model matrices.

It must never write interpolated values into:

```text
Actor position/orientation
Actor matrix storage
Actor pools
Global first-person authoritative transform
Physics or collision structures
Network snapshots
Animation or recoil state
```

If any prerequisite is unavailable for a particular frame or object, submit
the game's native data unchanged.

## 4. DFBHD target layout

### 4.1 Verified hooks

| Purpose | VA | RVA | Overwrite |
|---|---:|---:|---:|
| Coherent visual-clock capture | `0x004BF08F` | `0x000BF08F` | 5 bytes |
| Fast OITEM post-update capture | `0x004803EF` | `0x000803EF` | 5 bytes |
| MITEM post-update capture | `0x00472C2C` | `0x00072C2C` | 5 bytes |
| Slow OITEM post-update capture | `0x00472E63` | `0x00072E63` | 5 bytes |
| Actor render context | `0x00521D80` | `0x00121D80` | 7 bytes |
| Model submission A | `0x004FFB20` | `0x000FFB20` | 5 bytes |
| Model submission B | `0x004FFBA0` | `0x000FFBA0` | 8 bytes |
| First-person matrix-builder call | `0x00488AF6` | `0x00088AF6` | 5 bytes |
| Camera builder call | `0x004B751A` | `0x000B751A` | callsite identification |

The current camera/FOV module detours the camera builder itself at
`0x004181A0` and identifies the main call using return address `0x004B751F`.

### 4.2 Expected signatures

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

### 4.3 Relevant globals and structures

```text
OITEM pool pointer:       0x00715900
MITEM pool pointer:       0x00715904
Camera mode:              0x007F2DD0
Camera owner:             0x007F2DD4
Gameplay tick:            0x009F374C
Pause/loading state:      0x007C72BC
Scheduler phase:          0x00A0339C
Scheduler special mode:   0x00A033C0
Global FP transform:      0x0096C440 (read only)
Actor slot size:          0x29C
OITEM capacity:           256
MITEM capacity:           1200
```

Authoritative actor root:

```cpp
struct TransformQ16
{
    int32_t x;       // actor + 0x08
    int32_t y;       // actor + 0x0C
    int32_t z;       // actor + 0x10
    uint32_t yaw;    // actor + 0x14
    uint32_t pitch;  // actor + 0x18
    uint32_t roll;   // actor + 0x1C
};
```

Positions use signed Q16. Angles use a full unsigned 32-bit turn.

Identity/lifecycle fields used by the guarded implementation are:

```text
actor + 0x00 = definition index / active indicator
actor + 0x24 = definition pointer
actor + 0x4C = logical ID, when available
actor + 0x13C = ride/mount target
actor + 0x224 = slow-update callback
actor + 0x228 = fast movement callback
```

## 5. Shared visual clock

### 5.1 Why not use wall-clock camera sampling

A camera-only implementation can observe camera changes with QPC and blend
between samples. That smooths the camera but does not tell the viewmodel or
world actors which authoritative substep produced their transforms. Different
visual objects then occupy different timelines, which appears as shaking.

The synchronized implementation instead captures the game's native scheduler
state immediately after fixed-step catch-up and before visual rendering.

### 5.2 Native scheduler representation

The scheduler uses:

```text
1 scheduler unit = 1/16 ms
4 ms step         = 0x40 units
normal remainder  = 0..0x3F
```

At `0x004BF08F`:

```text
EBX                  = post-catch-up remainder
[0x00A0339C]         = post-increment substep phase
[0x007C72BC]         = pause/loading state
[0x00A033C0]         = special scheduler mode
```

Capture all of these as one visual-clock observation. The inline stub must
preserve every GPR and EFLAGS, especially `EBX`.

```cpp
struct VisualClock
{
    uint32_t remainder;
    uint32_t phase;
    bool valid;
};
```

The clock is valid only when:

```cpp
remainder < 0x40 && pauseState == 0 && specialMode == 0
```

Invalidate all interpolation history when pause or special-mode state changes.

### 5.3 Interpolation alpha

Use Q16 alpha where `0 == 0.0` and `65536 == 1.0`.

For a 250 Hz producer:

```cpp
alpha250 = remainder * 65536 / 64;
```

The 62.5 Hz callback executes while the pre-increment scheduler phase is zero.
The corresponding freshly captured snapshot therefore begins at
post-increment phase one:

```cpp
completedQuarters = (postIncrementPhase - 1) & 3;
units = completedQuarters * 64 + remainder;
alpha62 = units * 65536 / 256;
```

Expected phase mapping with zero remainder:

```text
phase 1 -> 0.00
phase 2 -> 0.25
phase 3 -> 0.50
phase 0 -> 0.75
```

This intentionally renders between the previous and current authoritative
snapshots and therefore adds up to one authoritative update of visual latency.

In another engine, prefer timestamps attached to each producer snapshot over
hardcoded phase formulas when a reliable simulation time is available:

```cpp
alpha = clamp(
    (renderTime - previous.time) /
    (current.time - previous.time),
    0.0,
    1.0);
```

## 6. Entity snapshots

### 6.1 Track state

Store snapshots independently from the game's actor memory:

```cpp
enum class Cadence { Unknown, Hz250, Hz62_5 };

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

Map an actor pointer to `(pool, slot)` only after proving that it lies inside a
known pool, is aligned to the actor stride, and is below the pool capacity.

### 6.2 Capture after the producer

Capture after authoritative code has finished updating a slot:

- `0x004803EF`: verified fast OITEM path, treated as 250 Hz;
- `0x00472E63`: verified slow OITEM path, treated as 62.5 Hz;
- `0x00472C2C`: verified MITEM path, treated as 62.5 Hz.

The common loop tails also execute for skipped slots. Filter actors by their
actual callback fields:

```cpp
fast OITEM:
    actor->moveFunction != nullptr &&
    actor->moveFunction != NullMovementCallback

slow OITEM:
    actor->moveFunction == nullptr &&
    actor->slowUpdateCallback != nullptr &&
    actor->slowUpdateCallback != NullMovementCallback
```

Revalidate the actor after a callback because the callback may destroy or
replace its slot.

### 6.3 Snapshot advancement

Advance timestamps even if the transform did not numerically change. Otherwise
a later movement incorrectly appears to span several producer intervals.

Avoid advancing twice within the same producer step:

```cpp
if (track.current.producerStep == step)
{
    track.current.transform = live;
}
else
{
    track.previous = track.current;
    track.current = {live, step, true};
}
```

### 6.4 Identity and rebasing

Rebase `previous = current = live` when:

- a slot becomes active;
- the definition index or pointer changes;
- a valid logical ID changes;
- the ride target changes;
- producer cadence changes;
- movement exceeds the teleport threshold;
- pause/special scheduler mode changes;
- world/tick continuity is reset;
- the live transform differs from `current` without a producer capture.

The last rule detects script, network, teleport, or other external writers that
bypass the verified producer hooks. Never interpolate stale state across an
unobserved authoritative correction.

This implementation uses a 64-world-unit per-axis Q16 teleport threshold:

```cpp
thresholdQ16 = 64 * 65536;
```

Choose a threshold appropriate for the scale and fastest legitimate motion in
the target project.

## 7. Position and angular interpolation

Position interpolation uses 64-bit intermediate arithmetic:

```cpp
int32_t LerpQ16(int32_t from, int32_t to, uint32_t alphaQ16)
{
    int64_t delta = int64_t(to) - from;
    return int32_t(from + delta * alphaQ16 / 65536);
}
```

Full-turn angles must follow the shortest modular path:

```cpp
uint32_t LerpAngle(uint32_t from, uint32_t to, uint32_t alphaQ16)
{
    uint32_t modular = to - from;
    int64_t shortest = modular <= 0x7fffffff
        ? int64_t(modular)
        : int64_t(modular) - 0x100000000LL;

    return from + uint32_t(shortest * alphaQ16 / 65536);
}
```

Do not cast `to - from` through signed overflow. Compute the modular delta as
unsigned and then expand it to 64 bits as shown.

## 8. Root correction matrices

### 8.1 Matrix convention

DFBHD final model matrices are `0x40`-byte row-major `float[4][4]` matrices
using row-vector composition. Translation is stored in the fourth row:

```text
m[3][0] = Tx
m[3][1] = Ty
m[3][2] = Tz
m[3][3] = 1
```

The native Q16 transform builder is `0x0055DB60`. Its world-to-render position
mapping is:

```cpp
renderX = worldX / 65536.0f;
renderY = worldZ / 65536.0f;
renderZ = -worldY / 65536.0f;
```

Do not reuse this multiplication order in another project until its matrix
convention is independently proven.

### 8.2 Correction derivation

For row-vector composition:

```text
NativeFinal = Local * CurrentRoot
WantedFinal = Local * InterpolatedRoot
```

Therefore:

```cpp
Correction = Inverse(CurrentRoot) * InterpolatedRoot;
WantedFinal = NativeFinal * Correction;
```

The correction is right-multiplied. Left multiplication would rotate or
translate around the wrong space, commonly producing world-origin orbiting.

Apply the correction to every final palette matrix when every matrix is already
in actor/world space. Applying it only to matrix zero can leave bones and child
attachments on the current authoritative root.

Use a guarded affine inverse. Reject non-affine, singular, non-finite, or
otherwise malformed matrices and submit the native data instead.

## 9. Camera correction

### 9.1 Safe boundary

The main renderer passes a `0x40`-byte stack-local camera source to the camera
builder. Relevant source fields are:

```text
+0x04/+0x08/+0x0C = signed-Q16 position
+0x10/+0x14/+0x18 = full-turn angles
+0x3C              = horizontal FOV
```

Only first-person camera mode zero and the verified main call are corrected.
Other camera modes use native behavior.

### 9.2 Apply an owner-root delta, not a replacement

The current camera source already includes eye height, bob, recoil, shake,
lean, vehicle offsets, and other current render effects. Replacing it with the
interpolated actor root would destroy those effects.

Instead:

```cpp
positionDelta = interpolatedOwner.position - currentOwner.position;
angleDelta = interpolatedOwner.angle - currentOwner.angle; // modular

stackCamera.position += positionDelta;
stackCamera.angle += angleDelta;
```

Copy all `0x40` bytes first, modify only the six root-derived fields in the
copy, and pass the copy to the original builder. FOV correction may then modify
the copy temporarily and restore it after the original call.

## 10. First-person viewmodel correction

The primary first-person path lets the game build its normal transform through
`0x004892F0`, stores it at `0x0096C440`, and calls `0x0055DB60` at
`0x00488AF6` to produce a stack-local matrix.

Replace only that five-byte call with a wrapper:

```cpp
void ViewmodelMatrixWrapper(Matrix4x4* destination,
                            const TransformQ16* source)
{
    OriginalBuildFloatMatrix(destination, source);

    if (!CanInterpolateCameraOwner())
        return;

    Matrix4x4 correction = BuildOwnerRootCorrection();
    *destination = Multiply(*destination, correction);
}
```

Do not edit the global first-person transform. Applying the correction after
the native builder preserves current weapon placement, ADS, recoil, sway,
inertia, and animation.

If another project renders hands and weapon through separate roots, locate and
correct both render-only matrices or explicitly leave the unknown path native.

## 11. World actor correction

### 11.1 Establish actor context

Detour the verified actor render dispatcher and maintain a thread-local stack:

```cpp
void HookRenderActor(Actor* actor, int arg1, int arg2)
{
    PushCurrentActor(actor);
    OriginalRenderActor(actor, arg1, arg2);
    PopCurrentActor();
}
```

Use a stack rather than a single pointer because render callbacks may be
nested. Do not use `Model*` as identity; models may be shared by many actors.

### 11.2 Clone at final submission

The verified submitters receive:

```cpp
void SubmitModel(Model* model, Matrix4x4* matrices);
```

The palette count is a signed 32-bit count at `model + 0x10`, matrix zero is
included, and every matrix has a `0x40` stride. The guarded implementation
accepts only `1..51` matrices.

```cpp
void HookSubmit(Model* model, Matrix4x4* matrices)
{
    Actor* actor = CurrentRenderActor();
    int count = model ? model->matrixCount : 0;

    if (!actor || !matrices || count < 1 || count > 51)
        return OriginalSubmit(model, matrices);

    Matrix4x4 correction;
    if (!BuildCorrection(actor, &correction))
        return OriginalSubmit(model, matrices);

    Matrix4x4* visual = GetThreadLocalBuffer(count);
    for (int i = 0; i < count; ++i)
        visual[i] = Multiply(matrices[i], correction);

    OriginalSubmit(model, visual);
}
```

The cloned storage must remain valid until the original submitter returns.
This design assumes synchronous pointer consumption; prove that property in a
new target before using a temporary buffer.

## 12. Hook installation

### 12.1 Inline capture stubs

The scheduler and producer sites are loop tails rather than normal function
entries. Their generated stubs perform:

```text
pushfd
pushad
push callback arguments
call capture callback
popad
popfd
execute displaced instruction
jump to original + displaced length
```

The callback uses `__stdcall` so it removes its generated arguments. Preserve
the original flags because loop control after the hook may depend on them.

### 12.2 Function detours

For the actor dispatcher and submitters:

1. compare the complete expected-byte signature;
2. allocate executable trampoline memory;
3. copy complete instructions, never partial instructions;
4. append a relative jump back;
5. make the target page writable;
6. write a relative jump and NOP remaining displaced bytes;
7. flush the instruction cache;
8. restore page protection.

Account for position-relative instructions when porting to x64. The DFBHD
sites described here use 32-bit x86 code and were selected so their displaced
instructions require no relative relocation.

### 12.3 Partial-install behavior

Keep the feature disabled until all required hooks have been validated and
installed. Every installed wrapper must execute the original native path while
the enabled flag is false. For production-quality patching, prefer a
transactional hook library that can roll back earlier hooks when a later hook
fails.

## 13. Fail-closed decision tree

Before applying any interpolation, require all relevant conditions:

```cpp
if (!featureEnabled ||
    !clock.valid ||
    !knownPoolAndSlot ||
    !stableIdentity ||
    !knownProducerCadence ||
    !previous.valid ||
    !current.valid ||
    liveTransform != current.transform ||
    !knownCameraModeOrRenderContext ||
    !validMatrixCount ||
    !finiteInvertibleRoot)
{
    SubmitNative();
}
```

The current guarded subset intentionally leaves these paths native:

- unknown producer cadences;
- DITEM/static categories without a proven producer;
- projectiles and temporary objects without verified coverage;
- auxiliary actor pools;
- special camera modes;
- model submissions outside a verified actor context;
- invalid or oversized matrix palettes;
- unobserved authoritative transform changes.

Expanding coverage should be done category by category, only after proving both
the authoritative producer and the render-only submission boundary.

## 14. Diagnostics

Track at least:

```text
snapshot captures
camera corrections
viewmodel corrections
world palette corrections
native fallback: no context
native fallback: invalid matrix count
native fallback: missing snapshot
unobserved authoritative changes
clock validity
```

In BHD_QoL these appear as `VisualInterpolation.Stats` during the existing Raw
Input statistics interval. Diagnostics are especially important during initial
runtime validation because successful hook installation does not prove correct
semantic coverage.

## 15. Tests

Unit-test all engine-independent math:

1. 250 Hz and 62.5 Hz phase mapping;
2. signed-Q16 position interpolation;
3. full-turn wraparound in both angular directions;
4. exact alpha endpoints;
5. affine inversion and singular rejection;
6. row-vector multiplication;
7. right-side root correction for translation;
8. root correction for rotation away from the world origin;
9. teleport detection;
10. entity generation and rebase rules;
11. duplicate producer-step suppression;
12. pause and clock invalidation.

Runtime validation should additionally verify:

- all hook signatures on the exact executable;
- expected snapshot counts for each cadence;
- camera/viewmodel/world correction counts while moving;
- no correction in menus or unsupported camera modes;
- native fallback for unknown objects;
- no actor-pool writes;
- no hitbox, muzzle, networking, or weapon-logic changes;
- correct focus, pause, map-change, death, mount, and teleport transitions;
- matrix-buffer lifetime under nested and repeated submissions.

## 16. Recommended implementation order in another project

1. **Recover the native clock.** Confirm the exact render phase without
   modifying visuals.
2. **Instrument authoritative producers.** Record snapshots and identity, but
   continue native rendering.
3. **Add diagnostics.** Verify cadence, slot reuse, pause, and teleports.
4. **Interpolate camera root only.** Preserve native camera effects by applying
   a delta to a render-local copy.
5. **Synchronize the viewmodel.** Correct the final local root matrix, not its
   authoritative input.
6. **Instrument actor submission context.** Verify nested calls and category
   coverage before modifying palettes.
7. **Enable translation-only world correction.** Validate coordinate and
   multiplication convention.
8. **Enable angular correction.** Test wraparound and non-origin rotation.
9. **Expand categories gradually.** Unknown paths remain native.
10. **Keep the option disabled by default** until runtime evidence confirms
    stability on the supported executable.

## 17. Latency and limitations

Interpolation produces smooth render-frequency motion but cannot create new
authoritative simulation information. This design renders between the previous
and current known states, so it adds up to one authoritative update of visual
latency—approximately 16 ms for the 62.5 Hz paths.

Prediction could reduce that latency, but prediction introduces overshoot and
correction artifacts and is outside this implementation. If minimum
mouse-to-photon latency is more important than smooth motion, leave the option
disabled.

The feature also does not increase animation sampling frequency by itself. It
moves already-built camera, viewmodel, and world matrices onto a synchronized
root timeline. Animation channels that are intrinsically updated at a lower
rate may still require a separate, render-only animation interpolation system.

## 18. Source map

The corresponding implementation is split across:

```text
src/visual_interpolation.cpp       hooks, snapshots, guards, corrections
src/visual_interpolation.h         runtime interface
src/visual_interpolation_math.h    portable transform/matrix math
src/visual_camera_math.h           linear and modular interpolation helpers
src/camera_fov.cpp                 camera stack-copy integration
src/dinput8.cpp                    configuration and installation
src/raw_input.cpp                  periodic statistics emission
tests/visual_interpolation_math_test.cpp
```

Treat the current implementation as an executable-specific worked example of
the architecture, not as a universal drop-in hook package.
