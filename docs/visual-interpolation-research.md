# Visual interpolation reverse-engineering notes

This document records static findings from the supported `DFBHD.EXE` while
investigating synchronized interpolation for the camera, first-person
viewmodel, and moving world entities. `Knowledge_Base/DFBHD.EXE` is a
read-only reference; no file in `Knowledge_Base/` is modified by this work.

## Confirmed camera boundary

The main renderer builds a 64-byte camera source on its stack and calls the
camera builder at `0x004B751A`. The return address is `0x004B751F`. The builder
at `0x004181A0` consumes Q16 position fields at source offsets
`+0x04/+0x08/+0x0C` and full-turn angle fields at `+0x10/+0x14/+0x18`, then
writes derived view data to the render-camera object at `0x00715930`.

This remains a safe boundary for camera-only interpolation because the source
is a stack copy. It is not a frame-wide interpolation boundary: immediately
after constructing the camera, the renderer invokes several independent
world, actor, effect, and first-person rendering paths between `0x004B75A1`
and `0x004B783D`.

## Confirmed actor storage

The executable maintains three contiguous actor pools. Their base pointers are
stored at `0x00715900`, `0x00715904`, and `0x00715908`, and each actor slot is
`0x29C` bytes. The pool walks at `0x004B4CA5`, `0x004B4CC7`, and `0x004B4CE8`
confirm the stride. A separate walk at `0x0043D570` uses counts at
`0x00683F10`, `0x0070DDDC`, and `0x00683F14` and shows that all three pools use
the same slot layout.

Actor position is a Q16 vector at slot offsets `+0x08/+0x0C/+0x10`. For
example, `0x00433857` takes the address of `actor + 0x08` as a three-component
position, while the player rendering path at `0x0050207F` does the same for
the actor referenced by the local-player pointer at `0x0096C290`.

These pools are authoritative storage, not render snapshots. They are
referenced throughout gameplay, AI, collision, cleanup, and rendering. The
indirect function at actor offset `+0x22C` is also not a universal render
callback: the calls at `0x0043D5B9`, `0x0043D607`, and `0x0043D655` occur while
matching and clearing actor slots, so treating it as a rendering hook would
corrupt lifecycle behavior.

## Viewmodel and world rendering are separate

After the camera is built, the first-person branch beginning at `0x004B75CC`
selects among multiple routines according to camera/player state. The path
later calls `0x00501FC0`, which reads the local actor from `0x0096C290`, builds
temporary matrices, and submits additional model data. Other branches call
different routines, including `0x004CDBC0`, so there is no single verified
viewmodel transform function shared by every weapon and camera mode.

World rendering is likewise split across multiple direct and definition-driven
paths. The object-definition parser recognizes the `render_function` field at
`0x00464F10`, demonstrating that different object types can select different
render functions. Consequently, hooking one apparent actor or model routine
would not cover all moving entities.

## What is still missing

A safe implementation needs two boundaries that static analysis has not yet
established:

1. **A visual snapshot boundary.** The code must identify where each active
   actor's authoritative Q16 transform is copied into frame-local render data.
   Interpolation must modify that copy, never the `0x29C` actor slot.
2. **A common submission boundary or complete callback map.** Every relevant
   actor render callback must be identified, including the local viewmodel,
   before the feature can guarantee that camera, weapon, and enemies use the
   same interpolation factor.

The required next trace starts from the render calls following `0x004B751F`
and follows writes into temporary 4x4 matrices and render queues, especially
calls receiving `actor + 0x08` or the render camera at `0x00715930`. For each
candidate hook, both callers and writes must be classified. A candidate is
safe only if its output is frame-local and no gameplay reader consumes it.

## Implementation constraints

When those boundaries are confirmed, the implementation should:

- capture previous/current transforms by stable actor identity rather than by
  slot address alone, because pool slots can be reused;
- compute one frame interpolation factor and share it with the camera,
  viewmodel, and all entity submissions;
- interpolate Q16 position linearly and angles by their shortest modular arc;
- snap on spawn, despawn, slot reuse, teleport, camera-mode transition, map
  load, and abnormal update gaps;
- leave authoritative actor slots, hitboxes, physics, AI, networking, audio,
  and weapon state untouched;
- fail closed if any executable signature or expected callsite differs.

Until both missing boundaries are verified, enabling camera-only interpolation
can place the camera and objects on different visual timelines. That explains
the observed shaking of moving enemies and the first-person weapon, and is why
writing interpolated values directly into the actor pools is not an acceptable
shortcut.
