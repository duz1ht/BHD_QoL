# DFBHD Scoped Mouse Precision Fix Tech Summary

## Purpose

This document describes the planned runtime fix for the scoped mouse precision asymmetry found in `DFBHD.exe`.

The fix is intended to be implemented from a proxy DLL such as `dinput8.dll`, while keeping the original `DFBHD.exe` unchanged on disk.

The goal is to remove the left/right asymmetry caused by fixed-point truncation when scope magnification reduces mouse sensitivity below one whole output unit per input count.

## Problem

DFBHD applies the configured mouse sensitivity as fixed-point arithmetic.

When a sniper scope is active, the game reduces the effective mouse scale by dividing it by the current scope magnification.

The relevant path in the analyzed build is approximately:

```text
mousescale
    ↓
mousescale << 11
    ↓
scope magnification lookup
    ↓
effectiveScale = baseScale / zoom
    ↓
effectiveScale × mouseDelta
    ↓
fixed-point conversion
    ↓
integer mouse movement
    ↓
bindings / yaw / pitch
```

The relevant code is located around:

```text
0x0045FE4C  base mouse scale setup
0x0045FE73  scope magnification lookup
0x0045FE7D  divide by magnification
0x0045FE82  scaled mouse calculation begins
0x0045FE85  X multiplication
0x0045FE88  X fixed-point conversion
0x0045FE94  Y fixed-point conversion
```

Addresses must be verified against the exact target executable build before installing hooks.

## Root Cause

The original code multiplies the effective fixed-point scale by the signed mouse delta and then discards the lower 16 fractional bits.

Conceptually:

```cpp
scaled = (effectiveScale * delta) >> 16;
```

The implementation uses signed multiplication followed by a fixed-point conversion equivalent to the original `IMUL + SHRD 16` path.

When the scoped sensitivity becomes fractional, for example `0.5`, the conversion becomes asymmetric around zero.

Example:

```text
+1 × 0.5 = +0.5  →  0
-1 × 0.5 = -0.5  → -1
```

Therefore, very small movement in one direction can disappear while the opposite direction still produces one unit of output.

This becomes especially visible with high-magnification sniper scopes because the effective sensitivity is reduced before the integer conversion.

## Why Simple Rounding Is Not Enough

A simple symmetric truncation fix such as:

```text
+0.5 → 0
-0.5 → 0
```

would remove the directional asymmetry, but it would still discard sub-unit movement.

That would create an equal dead zone in both directions.

The preferred solution is to preserve fractional movement across input updates.

## Corrected Model

The DLL should maintain a signed fractional remainder independently for the X and Y axes.

Example state:

```cpp
int64_t remainderX = 0;
int64_t remainderY = 0;
```

The corrected calculation should conceptually behave like:

```cpp
int32_t ScaleMouseWithRemainder(
    int32_t delta,
    int32_t scaleQ16,
    int64_t& remainder)
{
    int64_t value =
        (int64_t)delta * (int64_t)scaleQ16 +
        remainder;

    int32_t output =
        (int32_t)(value / 65536);

    remainder =
        value - (int64_t)output * 65536;

    return output;
}
```

The important property is that the fractional part is not discarded.

## Example: Effective Scale 0.5

Positive movement:

```text
Input +1
    ↓
0.5
    ↓
output 0
remainder +0.5

Input +1
    ↓
0.5 + 0.5
    ↓
output +1
remainder 0
```

Negative movement:

```text
Input -1
    ↓
-0.5
    ↓
output 0
remainder -0.5

Input -1
    ↓
-0.5 + -0.5
    ↓
output -1
remainder 0
```

Both directions now behave symmetrically.

## Direction Reversal

The signed remainder must be preserved when the player reverses direction.

Example:

```text
Input +1
    ↓
remainder +0.5

Input -1
    ↓
-0.5 + +0.5
    ↓
output 0
remainder 0
```

This prevents artificial movement from being generated when opposite sub-unit movements cancel each other.

## Hook Location

The fix should be applied at the stage where DFBHD has already calculated:

```text
effective scoped mouse scale
+
raw/internal mouse delta
```

but before the result is passed into the rest of the original mouse pipeline.

The preferred target is the block around:

```text
0x0045FE82
```

where the original X and Y fixed-point multiplication/conversion occurs.

The runtime hook should replace only the original fixed-point conversion behavior.

The rest of the DFBHD mouse processing should continue normally.

## Do Not Inject Directly Into Yaw or Pitch

The DLL should not write directly to player yaw or pitch.

Doing so would bypass original DFBHD behavior such as:

```text
mousescale
flipmouse
scope scaling
input bindings
context-sensitive input processing
temporal/input distribution
pitch limits
player-state rules
```

The fix should remain as close as possible to the original fixed-point scaling stage.

## Runtime Patch Through dinput8.dll

The intended architecture is:

```text
DFBHD.exe starts
    ↓
proxy dinput8.dll is loaded
    ↓
DLL validates executable version
    ↓
pattern scan locates scoped mouse scaling block
    ↓
DLL changes code protection with VirtualProtect()
    ↓
DLL installs mid-function hook / trampoline
    ↓
original mouse delta and effective scale reach hook
    ↓
DLL performs signed fractional accumulation
    ↓
corrected integer X/Y are returned
    ↓
execution resumes inside DFBHD
    ↓
original bindings / yaw / pitch processing continues
```

The original executable remains unchanged on disk.

## Pattern Scanning

The implementation should not rely only on a hard-coded virtual address.

Recommended approach:

```text
1. Identify a stable byte signature around the scaling block.
2. Pattern scan the DFBHD module at startup.
3. Require exactly one valid match.
4. Verify surrounding opcodes before patching.
5. Abort the feature safely if validation fails.
```

This makes the patch more robust across compatible executable variants.

## Conservative Activation

The corrected path should only run when it is actually needed.

A conservative condition is:

```cpp
if (scopeReductionActive &&
    (effectiveScale & 0xFFFF) != 0)
{
    // corrected fractional accumulation
}
else
{
    // preserve original DFBHD behavior
}
```

This keeps integer-scale mouse movement unchanged.

The goal is to modify only the situations where DFBHD would otherwise lose fractional precision.

## Remainder Reset Conditions

The DLL should reset:

```text
remainderX
remainderY
```

whenever the meaning of the accumulated fraction changes.

Recommended reset conditions:

```text
entering scope
leaving scope
changing zoom level
changing weapon if scope characteristics change
input reset
focus loss
relevant player/input state reset
```

This prevents a remainder generated under one zoom factor from being applied under another.

## Interaction With Raw Input

This fix is independent of whether the mouse source is:

```text
WM_MOUSEMOVE
Raw Input / WM_INPUT
```

The bug exists after DFBHD already has an integer mouse delta and applies scoped sensitivity.

Therefore the same fractional fix can work with:

```text
original DFBHD mouse input
or
modern Raw Input supplied by the proxy DLL
```

If Raw Input is enabled, the intended path becomes:

```text
WM_INPUT
    ↓
Raw Input delta
    ↓
DFBHD-compatible mouse delta
    ↓
original mousescale
    ↓
original scope magnification scaling
    ↓
fractional precision fix
    ↓
original bindings
    ↓
original yaw / pitch
```

## Configuration

Suggested configuration:

```ini
scoped_mouse_precision_fix = 1
```

Optional diagnostic mode:

```ini
scoped_mouse_precision_debug = 0
```

When debug is enabled, useful values to log include:

```text
raw/internal delta X/Y
base mouse scale
scope magnification
effective Q16 scale
fractional remainder X/Y
final integer X/Y
scope state
zoom level
```

## Validation

The implementation should be tested with:

```text
normal unscoped movement
Barrett scope
M21 scope
all available sniper zoom levels
very low sensitivity
default sensitivity
high sensitivity
1-count movement left
1-count movement right
1-count movement up
1-count movement down
continuous slow movement
rapid direction reversal
scope enter/exit
zoom-level changes
weapon changes
Alt-Tab / focus loss
original WM_MOUSEMOVE input
Raw Input mode
```

The expected result is:

```text
small positive and negative movements behave symmetrically
fractional movement is preserved over time
normal unscoped sensitivity remains unchanged
scope magnification scaling remains unchanged
yaw and pitch behavior remain original
```

## Summary

The scoped mouse issue is caused by DFBHD's signed fixed-point conversion discarding fractional precision asymmetrically after scope magnification reduces the effective mouse scale.

The fix should not replace the original sensitivity or aiming system.

Instead, the `dinput8.dll` should hook the existing fixed-point scaling stage, preserve the discarded signed fraction in per-axis accumulators, and return the corrected integer movement to the original DFBHD pipeline.

This keeps the executable unchanged on disk and preserves the original mouse sensitivity, scope scaling, bindings, yaw, pitch, and gameplay logic while removing the directional precision asymmetry.
