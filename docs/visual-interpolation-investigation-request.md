# DFBHD synchronized visual interpolation — investigation request

## Objective

Statically analyze the supported 32-bit `DFBHD.EXE` and identify safe hook
points for synchronized visual interpolation of:

- the main first-person camera;
- the local first-person weapon/viewmodel;
- moving actors and other world entities.

The implementation must never modify transforms consumed by physics,
collision, hitboxes, AI, networking, audio, weapon logic, or other gameplay
systems. Interpolation must operate only on frame-local render copies or final
visual matrices.

## Target executable

```text
SHA-256:   693676b5fb96012d32ee395ac23b4a8bf4d7dd9a9f25ae8e500749a2f0df34ea
Image base: 0x00400000
Format:     32-bit PE/i386
```

## Confirmed information

### Main camera

```text
Camera builder: 0x004181A0
Main callsite:  0x004B751A
Return address: 0x004B751F
Destination:    0x00715930
```

The main renderer constructs a camera-source structure of at least 64 bytes on
its stack. Confirmed source fields are:

```text
+0x04 = position X, signed Q16
+0x08 = position Y, signed Q16
+0x0C = position Z, signed Q16
+0x10 = angle 1, unsigned full-turn 32-bit
+0x14 = angle 2, unsigned full-turn 32-bit
+0x18 = angle 3, unsigned full-turn 32-bit
+0x3C = horizontal FOV, signed Q16
```

Camera-only interpolation is safe at this boundary because the source is a
stack copy. It is insufficient because the viewmodel and moving entities
remain on the authoritative timeline, producing relative shaking.

### Actor pools

```text
Pool base pointers: 0x00715900, 0x00715904, 0x00715908
Actor slot size:    0x29C bytes
Observed counts:    0x00683F10, 0x0070DDDC, 0x00683F14
Local actor pointer: 0x0096C290
```

Confirmed actor position:

```text
actor + 0x08 = X, signed Q16
actor + 0x0C = Y, signed Q16
actor + 0x10 = Z, signed Q16
```

The pools are authoritative and shared across gameplay and rendering. Do not
recommend writing interpolated values directly into them.

The indirect callback at `actor + 0x22C` is not a universal render callback.
Callsites `0x0043D5B9`, `0x0043D607`, and `0x0043D655` invoke it during actor
slot matching/cleanup and lifecycle processing.

### Known render split

The camera is followed by several independent rendering paths in
`0x004B751F–0x004B783D`. The first-person branch begins near `0x004B75CC`.
One path reaches `0x00501FC0`, which reads the local actor and builds temporary
matrices; other states use different routines, including `0x004CDBC0`.

The object-definition parser recognizes `render_function` near `0x00464F10`,
indicating that object types may use different render callbacks.

## Required findings

### 1. Visual snapshot boundary for entities

Find where authoritative actor position/orientation is copied or converted to:

- a frame-local render structure;
- a model matrix;
- a render-queue element;
- a visual command buffer;
- another output used only by rendering.

For every candidate provide:

```text
Proposed name:
VA and RVA:
Function signature:
Calling convention:
Arguments and return value:
Input structure and offsets:
Output structure and offsets:
All callsites:
Original bytes suitable for validation/detour:
Output lifetime:
Readers of the output:
Why it is safe or unsafe:
Pseudocode:
```

Trace calls after `0x004B751F`, particularly functions receiving `actor + 0x08`
or the render-camera object at `0x00715930`. A candidate is safe only when its
output is frame-local and has no gameplay readers.

### 2. Entity submission dispatcher or complete callback map

Determine whether there is a common function that iterates visible entities
and creates/submits their visual transforms. For each relevant path identify:

- the list or pool being traversed;
- stable entity identity;
- position and orientation fields;
- where the final model matrix is written;
- whether the function sees authoritative data or a visual copy;
- which object categories it covers or omits.

Look for render-queue structures resembling:

```cpp
struct RenderItem {
    void* entity;
    Matrix4x4 transform;
    void* mesh;
    // ...
};
```

If no common boundary exists, produce a complete callback map for at least:
players, enemies, vehicles, dropped weapons, animated objects, projectiles,
corpses, and relevant mission objects.

### 3. `render_function` mapping

Starting at the parser path near `0x00464F10`, determine:

1. where the parsed function pointer is stored in an object definition;
2. its exact definition offset;
3. whether/how it is copied into an instance;
4. the renderer-side indirect callsite;
5. callback signature and arguments;
6. all concrete functions assignable to it;
7. object categories handled by every callback;
8. whether all callbacks pass through a common wrapper first.

### 4. First-person viewmodel transform

Analyze the branch beginning near `0x004B75CC`, including `0x00501FC0`,
`0x004CDBC0`, and their callees. Identify:

- where the weapon/viewmodel transform is created;
- which camera source or matrix it consumes;
- whether it reads `0x00715930` or the local actor at `0x0096C290`;
- where base weapon placement, bob, sway, recoil, animation, zoom, and scope
  offsets are applied;
- where the final viewmodel matrix is stored;
- whether that matrix is temporary and render-only;
- the final submission function;
- how to distinguish the local viewmodel from weapons in the world;
- alternate paths for weapon types and camera modes.

The desired hook must reuse the same interpolated camera reference without
interpolating bob, sway, recoil, or weapon animation twice.

### 5. Stable actor identity and lifecycle

For each `0x29C` actor slot locate:

- active flag;
- pool and slot index;
- object/type identifier;
- generation, spawn, or unique identifier;
- definition pointer;
- model/animation pointer;
- destruction/death state;
- last-update tick, if present.

A slot address alone is insufficient because pool slots can be reused. Find a
key equivalent to:

```cpp
struct EntityKey {
    uint32_t pool;
    uint32_t slot;
    uint32_t generation;
};
```

If no generation exists, identify a reliable combination such as slot, object
type, and spawn ID.

Also locate reliable detection for spawn, despawn, death, destruction, slot
reuse, map load, mission restart, pause, camera-mode transition, vehicle
transition, and spectator transition. Provide addresses, signatures, callsites,
and validation bytes.

### 6. Actor orientation

Find the exact fields for body and visual orientation, including where
applicable:

- body yaw/pitch/roll;
- aim yaw/pitch;
- head orientation;
- animation-root rotation;
- vehicle/turret orientation.

For each field provide offset, width, signedness, representation, range,
direction, and whether it is authoritative or visual. Determine whether actor
angles use the camera's full-turn 32-bit representation or another format.

### 7. Authoritative tick and render alpha

Investigate `0x009F374C` and determine:

- exact meaning and increment site;
- update frequency;
- pause/loading behavior;
- wraparound behavior;
- whether actor transforms change on the same boundary;
- where the renderer reads it.

Also search for an existing simulation delta, previous/current transform,
render alpha, accumulator, prediction value, or interpolation mechanism. Look
for arithmetic equivalent to:

```cpp
render = previous + (current - previous) * alpha;
```

including Q16 sequences involving `sub`, `imul`, `shrd ..., 16`, and `add`.
Prefer reusing native previous-state and alpha data if available.

### 8. Matrix and render-queue functions

Map functions that:

- convert Q16 coordinates to floating point;
- create translation and rotation matrices;
- combine position and orientation;
- transform world space into view space;
- enqueue model transforms;
- submit transforms to the graphics wrapper or Direct3D.

For each function provide VA/RVA, signature, convention, arguments, return,
callers, output structure, and output lifetime.

## Expected final report

The report should include:

1. a simplified call graph of the main renderer;
2. separate paths for camera, world, characters, vehicles, viewmodel, and
   effects;
3. actor-pool and stable-identity layouts;
4. complete position/orientation layouts;
5. the first confirmed frame-local visual structure;
6. recommended hook points with expected-byte signatures;
7. paths not covered by those hooks;
8. evidence that gameplay cannot observe the interpolated copies;
9. pseudocode for synchronized interpolation.

## Completion criterion

The investigation is complete only when it supports this proven flow:

```text
authoritative tick
    ├── capture previous/current transform by stable identity
    └── record tick timestamp

visual frame start
    └── calculate one shared interpolation alpha

camera
    └── build from interpolated stack copy

viewmodel
    └── build from the same visual timeline and alpha

each visible moving entity
    └── build an interpolated frame-local matrix

renderer
    └── consume only visual copies
```

Do not propose direct writes to `0x00715900/04/08` actor pools. If no
frame-local visual boundary can be proven, explicitly report the unresolved
call chain rather than suggesting an authoritative-memory hook.
