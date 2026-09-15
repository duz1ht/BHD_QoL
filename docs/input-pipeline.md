# DFBHD mouse input pipeline

This document records the statically verified addresses for the supported
`DFBHD.EXE` (SHA-256
`693676b5fb96012d32ee395ac23b4a8bf4d7dd9a9f25ae8e500749a2f0df34ea`).
All modifications still validate their instruction bytes at runtime.

## Capture and consumption

- `0x005678F0` is the legacy mouse poll routine. It drains window mouse
  messages and converts the cursor position relative to the fixed center into
  `0x00F655EC` (X) and `0x00F655F0` (Y).
- The main consumer calls that routine at `0x004B49CB`. The values are read
  immediately at `0x004B49D0` and `0x004B49D5`, before the remainder handling
  beginning at `0x004B49DB`. The Raw Input detour therefore already replaces
  capture at the last safe point immediately before the game consumes it;
  writing game globals asynchronously from `WM_INPUT` would introduce races.
- Other static references at `0x0045FE2D`/`0x0045FE3D` feed the player aiming
  path. References around `0x004E33A4` classify the dominant movement axis for
  a separate input/event path and do not update camera angles.

## Sensitivity scaling

The player aiming path reads the configured base sensitivity at `0x009F20E4`,
converts it to Q16 at `0x0045FE4C`, and may replace that scale through the calls
between `0x0045FE58` and `0x0045FE7F`. X and Y are multiplied and truncated at
`0x0045FE82` through `0x0045FE9B`.

`MouseScalingFix` hooks that unique multiplication block. Fractional remainder
preservation applies to every fractional Q16 scale, including the base scale;
integral scales retain the original operation. Axis state is independent and a
scale transition resets only the affected axis.

## Runtime diagnostics

With logging enabled, `RawInput.Latency` reports:

- average age of the oldest and newest report consumed by a non-empty poll;
- maximum oldest-report age and reports grouped into a poll;
- empty polls;
- average and maximum poll interval, a direct measure of input-consumption
  pacing and a useful frame-pacing proxy;
- the QPC frequency used for all timing.

`MouseScaling.Stats` reports input/output totals, fractional calls, and scale
transitions. These aggregated diagnostics avoid I/O and allocation in the
per-report path.

Optional `FramePacingDiagnostics` hooks Direct3D 8 device creation, reset, and
presentation. `FramePacing.Config` records the requested presentation interval
and `FramePacing.Stats` correlates frame time with the latest input poll, records
the time blocked inside `Present`, separates stalls of at least 250 ms from the
steady-state frame average, and reports the actual duration of each sample
window. Input-poll counts and Presents per poll make the fixed camera/update
cadence visible without changing the game's authoritative simulation. This
measures the CPU-side call to `Present`; measuring when the pixel physically
appears still requires an external latency tool.

After startup or input reactivation, movement reports are kept out of the game
accumulators until its first mouse poll. Buttons, wheel events, and virtual menu
cursor updates continue normally. That first poll returns zero movement, logs
the discarded pre-poll totals, and immediately verifies full-client cursor
confinement before regular accumulation begins.

## Render frame limit

`RenderFrameLimit` optionally paces calls to Direct3D 8 `Present` using an
accumulated QPC deadline. Long stalls reset the deadline instead of triggering
a burst of catch-up frames.

## High-frequency visual camera

The main renderer constructs its camera source on the stack and calls the
camera builder at `0x004B751A`. The return address `0x004B751F` uniquely
distinguishes this call from map, preview, and auxiliary cameras. Static
analysis of the builder at `0x004181A0` establishes that source offsets
`0x04`/`0x08`/`0x0C` are the camera position and `0x10`/`0x14`/`0x18` are its
three full-turn 32-bit angles. The builder copies these fields into the render
camera and derives the matrices; it does not write them back to simulation.

`HighFrequencyVisualCamera` now uses the scheduler remainder captured after
fixed-step catch-up rather than estimating the update interval from QPC camera
changes. Post-update hooks retain previous/current actor roots for known OITEM
and MITEM producers. One scheduler-derived phase is then shared by the camera,
primary first-person viewmodel, and supported world-actor submissions.

The camera receives only the interpolated owner-root delta on its 64-byte stack
copy, preserving current eye-height, bob, recoil, shake, and lean. The
viewmodel receives the same correction after its native root matrix is built.
World actors retain native animation and bone construction; the DLL clones a
validated palette of 1--51 final matrices and right-multiplies each clone by
`inverse(currentRoot) * interpolatedRoot` immediately before synchronous
submission. No authoritative actor or global viewmodel transform is changed.

Unknown producers, untracked actor categories, special camera modes,
projectiles, temporary objects, auxiliary pools, invalid palettes, and
submissions without a proven actor context fail closed to their native visual
path. Unexpected transform changes and movement larger than 64 world units
rebase the snapshots. This guarded coverage avoids making the known camera,
viewmodel, people, and vehicle paths wait for universal object coverage.
