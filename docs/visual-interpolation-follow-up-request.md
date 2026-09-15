# DFBHD synchronized visual interpolation — follow-up investigation request

## Purpose

This is a focused follow-up to
[`visual-interpolation-investigation-request.md`](visual-interpolation-investigation-request.md).
The broad architecture and the likely render-only hook boundaries have already
been identified. Do not repeat the earlier general investigation. Resolve the
specific unknowns below so synchronized interpolation can be implemented
without allowing gameplay systems to observe visual transforms.

The intended result is one shared visual timeline for:

- the main first-person camera;
- the local first-person weapon/viewmodel;
- moving actors, vehicles, and other rendered world entities.

## Exact target

Analyze this executable, not a structurally similar build:

```text
SHA-256:   693676b5fb96012d32ee395ac23b4a8bf4d7dd9a9f25ae8e500749a2f0df34ea
Image base: 0x00400000
Format:     32-bit PE/i386
```

If the analyzed executable has a different hash, stop and report the mismatch.
Addresses from another build are not sufficient evidence even when the
surrounding code looks similar.

## Findings that may be treated as hypotheses to verify

The previous investigation identified the following candidate architecture:

```text
4 ms movement pass          0x00480340
16 ms gameplay update       0x004B4970
visual-frame function       0x004B5C30
main renderer               0x004B7110
camera callsite             0x004B751A -> 0x004181A0
first-person renderer       0x00488A10
actor render dispatcher     0x00521D80
model submission A          0x004FFB20
model submission B          0x004FFBA0
unsafe general matrix path  0x004FD850
```

Additional working hypotheses are:

- `0x009F374C` is the nominal 62.5 Hz gameplay tick counter;
- `0x00A0339C & 3` is the phase of the four 4 ms substeps in a 16 ms
  gameplay interval;
- actors can be updated by `actor + 0x228` during the 4 ms movement pass;
- the main pools are referenced by `0x00715900`, `0x00715904`, and
  `0x00715908`, with `0x29C`-byte slots;
- actor position is signed Q16 at `+0x08/+0x0C/+0x10`;
- actor yaw, pitch, and roll are full-turn 32-bit values at
  `+0x14/+0x18/+0x1C`;
- the definition's main render callback is at `definition + 0x114`;
- the first-person path builds a transform through `0x004892F0` and global
  storage around `0x0096C440`, then reaches `0x0055DB60`;
- `0x004FFB20` and `0x004FFBA0` appear to consume final model matrices;
- `0x004FD850` must not be used as a global interpolation hook because it is
  reachable by non-final and possibly gameplay-related transform queries.

Verify all hypotheses against the exact target. Clearly label any claim that
remains inferred rather than proven.

## Missing evidence required for implementation

### 1. Exact scheduler accumulator and atomic visual-clock capture

Locate the variable containing the scheduler remainder after fixed-step
catch-up. The earlier report described:

```text
accumulator unit = 1/16 ms
one 4 ms substep = 0x40 units
post-catch-up remainder range = [0, 0x40)
```

For the accumulator and phase counter, provide:

```text
absolute VA and RVA;
storage width and signedness;
all important readers and writers;
the instruction sequence that adds elapsed time;
the catch-up loop and subtraction instruction;
the exact point immediately before visual rendering where both values are
coherent;
expected bytes for a hook or inline capture at that point;
register and stack state at the proposed capture point;
pause, loading, reset, overflow, and long-stall behavior;
pseudocode that reproduces the native clock.
```

Confirm or correct these formulas:

```cpp
alpha250 = remainder / 64.0;
alpha62 = ((phaseWithin16ms * 64) + remainder) / 256.0;
```

Explain whether `phaseWithin16ms` must be sampled before or after a phase
increment. A one-substep phase error would put the camera and entities on
different timelines.

### 2. Exact authoritative snapshot boundaries

Determine where snapshots must be captured *after* actor transforms have been
updated. Merely identifying `0x00480340` and `0x004B4970` as update functions is
not enough.

Provide exact post-update sites for:

- actors updated through `actor + 0x228` in the 4 ms pass;
- actors whose transforms change only in the 16 ms gameplay update;
- transforms changed outside either normal boundary;
- the local player/camera owner;
- vehicles and mounted actors;
- projectiles and temporary moving objects, if they use a separate path.

For each site provide calling convention, live actor pointer, expected bytes,
register preservation requirements, and whether a callback can destroy or
replace the actor. Determine how an implementation can classify each tracked
entity's producer cadence instead of applying a universal 62.5 Hz alpha.

Also determine whether capturing an unchanged transform should advance its
snapshot timestamp. Explain how to avoid duplicate captures when one actor is
visited by multiple update passes.

### 3. Proven model layout and matrix-array bounds

Fully recover the model structure consumed by `0x004FFB20` and `0x004FFBA0`.
The earlier statement that the matrix count is “approximately `model+0x10`” is
not safe enough for allocating or copying memory.

Prove:

```text
the exact matrix-count offset and field width;
whether the value is a count, maximum index, bone count, or another quantity;
whether the root matrix is included;
matrix stride and alignment;
valid minimum and maximum values;
whether either submitter expects a different structure;
ownership and lifetime of the incoming array;
whether submission consumes the array synchronously;
whether the callee or graphics backend retains the pointer;
behavior when the matrix pointer is null or the count is zero.
```

Provide disassembly evidence from both producers and consumers. Identify a
defensive upper bound suitable for failing closed if runtime data is invalid.

### 4. Matrix convention and exact root-correction operation

Determine the precise `Matrix4x4` layout and convention used at the two model
submission functions:

- row-major versus column-major storage;
- row-vector versus column-vector multiplication;
- location of translation;
- handedness and world-to-render axis conversion;
- semantic operation implemented by `0x0055D780`;
- input/output aliasing rules for multiplication;
- semantic operation and failure conditions of `0x0055D920`.

Starting from a known actor transform, show the expected floating-point root
matrix and prove the exact correction formula and application side. Resolve
which of these, if either, is correct:

```cpp
correction = inverse(currentRoot) * interpolatedRoot;
visual[i] = visual[i] * correction;
```

```cpp
correction = interpolatedRoot * inverse(currentRoot);
visual[i] = correction * visual[i];
```

Specify whether correction should be applied to every matrix, only the root,
or a particular subset. Include worked examples for translation, yaw rotation,
and a non-origin actor so the implementation can be unit-tested independently.

### 5. Actor-render context and submission coverage

Verify the full ABI of `0x00521D80`, `0x004FFB20`, and `0x004FFBA0` against the
exact target. For each function provide:

```text
calling convention;
complete argument list;
return type;
callee/caller stack cleanup;
register preservation;
all relevant callsites;
expected entry bytes;
safe detour length and displaced-instruction relocation requirements.
```

Prove whether a thread-local `CurrentRenderActor` established around
`0x00521D80` correctly identifies every model submitted by that actor. Check:

- recursive or nested actor rendering;
- attachments, held weapons, occupants, and child models;
- multiple submissions for one actor;
- submissions deferred until after `0x00521D80` returns;
- submissions made on another thread;
- models shared by more than one actor;
- effects and UI models submitted inside actor context;
- world entities that bypass `0x00521D80`;
- static objects that should be deliberately excluded.

Produce a coverage table for players, enemies, corpses, vehicles, mounted
weapons, dropped weapons, projectiles, doors, mission objects, and effects.

### 6. Exact first-person viewmodel injection boundary

Analyze `0x00488A10` instruction by instruction around its calls to
`0x004892F0` and `0x0055DB60`. Identify a hook/callsite substitution that can
pass a private transform copy to the matrix builder without modifying
`0x0096C440`.

Provide:

```text
the exact transform structure size;
every field and representation used by 0x0055DB60;
the precise instruction addresses before and after each relevant call;
expected bytes and safe patch length;
live registers and stack layout;
the destination matrix and its lifetime;
all alternate viewmodel paths and camera modes;
how to distinguish the local viewmodel from world weapons;
whether hands and weapon use one root or separate submissions.
```

Trace where native ADS placement, bob, sway, recoil, animation, and camera
shake are introduced. State exactly which root delta should be applied so
those effects remain native and are not interpolated twice.

### 7. Camera source versus authoritative-root relationship

The existing implementation interpolates the stack-local camera source at
`0x004B751A`. To synchronize it with world entities and the viewmodel, identify
the authoritative transform(s) from which that stack structure is derived.

Prove:

- which actor/camera-owner fields feed camera position and angles;
- where first-person eye height, lean, recoil, shake, bob, spectator offsets,
  and vehicle offsets are added;
- whether the camera source and actor root use the same coordinate space;
- how to separate the tick-driven root component from current render effects;
- the exact position and angular delta that should be added to the stack copy;
- reset rules when camera owner or camera mode at `0x007F2DD4/0x007F2DD0`
  changes.

The answer must prevent both removing native camera effects and applying the
same interpolation twice.

### 8. Pool and lifecycle coverage

Confirm the identity and count source for every relevant pool, including the
additional pointers at `0x0071590C` and `0x00715910`. Determine whether moving
or renderable entities in those pools can reach either model submitter.

For each pool provide:

```text
purpose and entity categories;
base-pointer storage;
count or capacity source;
slot stride;
active/inactive test;
allocation and destruction functions;
stable logical identifier, if any;
render traversal and dispatcher;
whether synchronized interpolation must include or exclude it.
```

Verify lifecycle/reset sites and expected bytes for:

- allocation at or near `0x0043C560`;
- destruction at or near `0x004418B0`;
- world reset at or near `0x00414C30`;
- tick resets near `0x004BC208` and `0x0046E5A2`;
- pause/loading state at `0x007C72BC`;
- death-state transition associated with `actor+0x20 & 0x4`;
- ride-target changes at `actor+0x13C`.

Recommend a proven external generation key for entities whose logical ID at
`actor+0x4C` is zero.

## Required output format

Return one report organized in the same order as the eight missing-evidence
sections above. For every address or structure claim include:

```text
Confidence: confirmed | strongly supported | inferred | unknown
Evidence: function/callsite addresses and relevant disassembly
Exact-target verification: yes | no
Implementation consequence: what may safely be hooked, copied, or rejected
```

For every proposed hook include:

```text
VA and RVA
calling convention and prototype
expected original bytes
minimum overwrite length
relocation/trampoline requirements
register and flags preservation
reentrancy/threading considerations
all known callsites
fail-closed validation behavior
```

Include final tables for:

1. verified hook sites;
2. verified structure layouts;
3. world-entity coverage and omissions;
4. invalidation/rebase events;
5. unresolved risks that still block implementation.

Do not present an implementation as safe when any of the following remains
unproven:

- scheduler clock capture coherence;
- matrix-array bounds;
- matrix multiplication convention;
- viewmodel private-copy injection ABI;
- actor-to-submission context coverage.

## Safety constraints

The final design must never write interpolated values into:

```text
actor + 0x08/+0x0C/+0x10
actor + 0x14/+0x18/+0x1C
actor + 0x88
the actor pools referenced at 0x00715900/04/08
the global first-person transform at 0x0096C440
```

Do not recommend a global interpolation detour at `0x004FD850` or a global
behavior change at `0x004892F0`. Both have consumers outside the intended
render-only boundary.

All interpolation must operate on stack-local camera data, DLL-owned transform
copies, or DLL-owned copies of final model matrices. If a render-only lifetime
cannot be proven, report the path as unresolved rather than proposing an
authoritative-memory overwrite.

## Completion criterion

The follow-up is complete only when it proves enough detail to implement and
test this exact flow:

```text
authoritative updates
    -> capture previous/current transforms after their real producer updates

visual-frame boundary
    -> atomically capture one scheduler-derived render time

camera
    -> apply interpolated authoritative-root delta to stack-local camera copy
    -> retain current native shake/bob/recoil offsets

viewmodel
    -> copy the normal first-person root transform
    -> apply the same visual-root correction to the private copy
    -> retain native ADS/recoil/sway/animation

world actor submission
    -> resolve the actor from proven render context
    -> clone a proven, bounded final matrix array
    -> apply the proven root correction exactly once

renderer
    -> synchronously consume visual copies only
```

The report should finish with a binary decision:

```text
SAFE TO IMPLEMENT
```

or:

```text
NOT YET SAFE TO IMPLEMENT
Blocking evidence: ...
```
