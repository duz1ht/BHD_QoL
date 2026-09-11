# DFBHD Client-Only Visual FOV 90 Tech Summary

## Purpose

This document describes a clean way to render the main first-person camera at **90 degrees FOV** while keeping the normal DFBHD gameplay and network FOV state at the vanilla value of **80 degrees**.

The intended architecture is:

```text
Gameplay / network state:
80 degrees

Camera / renderer:
90 degrees
```

The important part is that the modification is applied only to the temporary camera structure immediately before DFBHD builds the render camera. It does **not** change the normal FOV globals used by the game and network code, and it does **not** patch or rewrite the outgoing network packet.

This is preferable to changing the gameplay FOV to 90 and later replacing the value in the network packet.

---

## Target executable

Analysis was performed against:

```text
File: DFBHD(2).EXE
Type: PE32, Intel i386
ImageBase: 0x00400000
SHA-256:
6fb91fe9da26a1284638b07041f4d936290823575334b305d7441ca606e4bb19
```

All absolute addresses in this document refer to that executable build.

For a runtime DLL implementation, prefer:

```cpp
runtimeAddress = moduleBase + RVA;
```

instead of assuming the module is always loaded at `0x00400000`.

---

# 1. DFBHD FOV representation

DFBHD stores these FOV values as signed 16.16 fixed-point integers.

Conversion:

```text
Q16.16 = degrees * 65536
degrees = Q16.16 / 65536
```

Important values:

```text
80 degrees = 80 * 65536
           = 5,242,880
           = 0x00500000

90 degrees = 90 * 65536
           = 5,898,240
           = 0x005A0000
```

---

# 2. Important FOV globals

The main FOV state is:

```text
0x00B63DE0
Default/base FOV
Normally 0x00500000 = 80 degrees

0x00B635A4
Target FOV

0x00B635A0
Current/interpolated FOV
```

Relevant RVAs:

```text
0x00B63DE0 -> RVA 0x00763DE0
0x00B635A4 -> RVA 0x007635A4
0x00B635A0 -> RVA 0x007635A0
```

The normal game state should remain at 80 degrees when using the client-only visual FOV modification:

```text
B635A0 = 0x00500000
B635A4 = 0x00500000
```

Do not set either of these globals to 90 degrees for this implementation.

---

# 3. Normal DFBHD FOV flow

The relevant flow is:

```text
0x00B63DE0
default/base FOV
        |
        v
0x00B635A4
target FOV
        |
        v
interpolation
0x004F5E9F - 0x004F5EC0
        |
        v
0x00B635A0
current FOV
        |
        v
main renderer
0x004B7110
        |
        v
temporary camera structure
FOV at +0x3C
        |
        v
camera builder
0x004181A0
        |
        +--> frustum calculations
        |
        v
0x0071596C
render camera FOV
        |
        v
projection setup
0x004FBCC0
        |
        v
D3D8 projection matrix
```

---

# 4. FOV interpolation

The target FOV is stored in:

```text
0x00B635A4
```

The current FOV is stored in:

```text
0x00B635A0
```

The update logic around:

```text
0x004F5E9F - 0x004F5EC0
```

is approximately:

```cpp
current = *(int32_t*)0x00B635A0;
target  = *(int32_t*)0x00B635A4;

current += (target - current + 0x1F) >> 5;

*(int32_t*)0x00B635A0 = current;
```

This is why changing the target can cause a smooth FOV transition.

The client-only visual FOV method described here does not modify this system.

---

# 5. Map FOV event

DFBHD contains a map event that can change the FOV target.

Handler:

```text
0x00495EC0
```

Relevant operation:

```asm
mov eax,[ebp+08]
mov ecx,[eax]
shl ecx,10h
mov [00B635A4],ecx
```

Conceptually:

```cpp
FovTarget = MapFovValue << 16;
```

Example:

```text
Map FOV 60
60 << 16
0x003C0000
60 degrees
```

This is important because a safe visual FOV override should not blindly force every camera state to 90 degrees.

---

# 6. Main renderer camera structure

The main rendering function is around:

```text
0x004B7110
```

It constructs a temporary camera structure on its stack.

The structure begins approximately at:

```text
EBP - 0x58
```

The camera FOV field is:

```text
camera + 0x3C
```

Therefore, in this function it corresponds to:

```text
EBP - 0x1C
```

In the normal camera path:

```asm
004B7406  mov ecx,[00B635A0]
004B740C  mov [ebp-1C],ecx
```

So the normal path is:

```text
B635A0
   |
   v
temporaryCamera + 0x3C
```

---

# 7. Special camera and zoom paths

Do not blindly replace every camera FOV with 90 degrees.

The renderer contains special paths that intentionally generate other FOV values.

Examples found during analysis include:

```text
A path that multiplies the normal FOV by approximately 0.8

A zoom-related path that applies another multiplier

A path that explicitly uses:
0x00140000 = 20 degrees
```

Because of these modes, the override should be applied only when the temporary camera itself is still using the normal vanilla 80-degree FOV.

This preserves scopes, zoom states, map-controlled FOV, and special cameras.

---

# 8. Camera builder

The temporary camera is passed to:

```text
0x004181A0
```

from the main render path:

```asm
004B750C  lea ecx,[ebp-58]
004B750F  push ecx
004B7510  push 00715930
004B751A  call 004181A0
004B751F  ...
```

This gives:

```text
Argument 1:
0x00715930
destination/global render camera

Argument 2:
EBP-0x58
temporary source camera
```

The call return address is:

```text
0x004B751F
```

This is useful for identifying this exact call to the camera builder.

---

# 9. Why 0x004181A0 is the preferred hook point

At the beginning of `0x004181A0`:

```asm
004181A0  push ebp
004181A1  mov ebp,esp
...
004181AB  mov esi,[ebp+0C]
004181AE  mov eax,[esi+3C]
```

The function immediately reads:

```text
sourceCamera + 0x3C
```

which is the camera FOV.

It then uses that value in the calculations that build the view/frustum.

This is important.

If the modification were made only later, at the final D3D projection matrix, the camera could render a 90-degree projection while earlier visibility/frustum calculations were still based on 80 degrees.

That could cause incorrect edge culling.

Changing the temporary camera FOV before `0x004181A0` processes it allows the following systems to agree:

```text
visual FOV
frustum
camera calculations
projection matrix
```

while leaving the gameplay/network FOV globals untouched.

---

# 10. Global render camera FOV

The destination camera begins at:

```text
0x00715930
```

Its FOV field is also at:

```text
+0x3C
```

therefore:

```text
0x00715930 + 0x3C
=
0x0071596C
```

Later, the renderer converts this Q16.16 FOV to floating-point degrees:

```asm
fild DWORD PTR [0071596C]
fmul DWORD PTR [00611590]
```

The constant at:

```text
0x00611590
```

is:

```text
1 / 65536
```

So:

```text
0x00500000 -> 80.0f
0x005A0000 -> 90.0f
```

The resulting value is then used by the projection setup routine around:

```text
0x004FBCC0
```

---

# 11. Recommended architecture

Keep the normal game FOV untouched:

```text
B635A0 = 80 degrees
B635A4 = 80 degrees
```

Then modify only:

```text
temporaryCamera + 0x3C
```

immediately before the camera builder processes it.

Desired result:

```text
GAME STATE

B635A0
80 degrees

B635A4
80 degrees

        |
        +------------------------------+
                                       |
                                       v
                              normal game/network state


RENDER CAMERA

temporaryCamera + 0x3C
80 degrees
        |
        | client visual hook
        v
90 degrees
        |
        v
0x004181A0
        |
        +--> frustum calculated for 90 degrees
        |
        v
0x0071596C
90 degrees
        |
        v
0x004FBCC0
        |
        v
D3D8 projection at 90 degrees
```

---

# 12. Network behavior

The previously mapped client world-data packet uses the normal gameplay FOV globals.

Relevant packet fields:

```text
Packet 0x3F

payload +0x28
<- 0x00B635A0
current FOV

payload +0x2C
<- 0x00B635A4
target FOV
```

The writes occur around:

```asm
004284B4  mov edi,[00B635A0]
004284BA  mov [eax-04],edi

004284C4  mov edi,[00B635A4]
004284CA  mov [eax-04],edi
```

With the visual-only design:

```text
B635A0 = 80
B635A4 = 80
```

so the normal packet builder naturally sends:

```text
current FOV = 80
target FOV  = 80
```

No network hook is necessary.

No packet field needs to be rewritten.

This is one of the main reasons this method is cleaner than changing `B635A0/B635A4` themselves.

---

# 13. Recommended hook conditions

Do not apply the 90-degree override unconditionally.

For the normal first-person 80-degree state, require all of the following:

```cpp
currentFov == 0x00500000
targetFov  == 0x00500000
sourceCameraFov == 0x00500000
```

Also restrict the hook to the camera builder invocation originating from the main render path.

Expected return address for this build:

```text
0x004B751F
```

RVA:

```text
0x000B751F
```

The resulting condition is conceptually:

```cpp
if (caller == MainRendererCameraBuildReturn &&
    currentFov == FOV_80 &&
    targetFov == FOV_80 &&
    sourceCameraFov == FOV_80)
{
    sourceCameraFov = FOV_90;
}
```

This intentionally avoids touching camera states that are already using a different FOV.

---

# 14. Hook implementation concept

`0x004181A0` uses two stack arguments and the caller ignores its return value.

A practical detour can treat it conceptually as:

```cpp
using BuildCameraFn = void (__cdecl *)(void* destinationCamera,
                                      void* sourceCamera);
```

Verify the exact prototype in the implementation environment before relying on it for anything beyond these two known arguments.

Useful constants:

```cpp
static constexpr int32_t FOV_80 = 0x00500000;
static constexpr int32_t FOV_90 = 0x005A0000;

static constexpr uintptr_t RVA_BUILD_CAMERA       = 0x000181A0;
static constexpr uintptr_t RVA_RENDER_RETURN      = 0x000B751F;

static constexpr uintptr_t RVA_CURRENT_FOV        = 0x007635A0;
static constexpr uintptr_t RVA_TARGET_FOV         = 0x007635A4;

static constexpr size_t CAMERA_FOV_OFFSET         = 0x3C;
```

Conceptual hook:

```cpp
BuildCameraFn g_OriginalBuildCamera = nullptr;

void __cdecl HookBuildCamera(void* destinationCamera, void* sourceCamera)
{
    const uintptr_t moduleBase =
        reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));

    const uintptr_t expectedReturn =
        moduleBase + RVA_RENDER_RETURN;

    const uintptr_t caller =
        reinterpret_cast<uintptr_t>(_ReturnAddress());

    int32_t* currentFov =
        reinterpret_cast<int32_t*>(moduleBase + RVA_CURRENT_FOV);

    int32_t* targetFov =
        reinterpret_cast<int32_t*>(moduleBase + RVA_TARGET_FOV);

    int32_t* cameraFov = nullptr;

    if (sourceCamera)
    {
        cameraFov = reinterpret_cast<int32_t*>(
            reinterpret_cast<uint8_t*>(sourceCamera) + CAMERA_FOV_OFFSET);
    }

    bool useVisual90 =
        caller == expectedReturn &&
        cameraFov != nullptr &&
        *currentFov == FOV_80 &&
        *targetFov == FOV_80 &&
        *cameraFov == FOV_80;

    int32_t originalCameraFov = 0;

    if (useVisual90)
    {
        originalCameraFov = *cameraFov;
        *cameraFov = FOV_90;
    }

    g_OriginalBuildCamera(destinationCamera, sourceCamera);

    if (useVisual90)
    {
        *cameraFov = originalCameraFov;
    }
}
```

The temporary FOV is restored after the original function returns.

This keeps the modification confined to the synchronous camera-building operation.

---

# 15. Why restore the temporary value

The source camera structure belongs to the original renderer.

Even though it is temporary, restoring the field after `0x004181A0` returns is safer than leaving it modified.

The hook should therefore perform:

```text
save original sourceCamera+0x3C
        |
        v
write 90 degrees
        |
        v
call original 0x004181A0
        |
        v
restore original sourceCamera+0x3C
```

The global render camera produced by `0x004181A0` has already received and processed the 90-degree value at that point.

---

# 16. Do not patch the packet

For this design, do not hook:

```text
0x00428330
```

for the purpose of modifying FOV.

Do not rewrite:

```text
packet +0x28
packet +0x2C
```

The packet should remain completely vanilla.

Because `B635A0` and `B635A4` remain 80 degrees, the vanilla packet code already produces the desired network state.

---

# 17. Do not change B635A0 or B635A4 to 90

Avoid this design:

```text
B635A0 = 90
B635A4 = 90
        |
        v
renderer = 90
        |
        v
rewrite network packet back to 80
```

It unnecessarily modifies gameplay state and then requires compensating changes in the network path.

Prefer:

```text
B635A0 = 80
B635A4 = 80

temporary camera only = 90
```

---

# 18. Why not hook only the final projection matrix

A tempting location is:

```text
0x004FBCC0
```

because this function builds the final projection.

That is not the preferred location.

`0x004181A0` already consumes the FOV when generating camera/frustum information.

If `0x004181A0` receives 80 degrees but the final projection receives 90 degrees, the game can end up with:

```text
frustum = 80
projection = 90
```

The visible screen would be wider than the visibility calculations expect.

Potential symptoms include objects disappearing or appearing incorrectly near the left and right edges.

The desired state is:

```text
frustum = 90
projection = 90
```

which is achieved by changing the temporary camera FOV before `0x004181A0` processes it.

---

# 19. Why not patch 0x0071596C afterward

Similarly, writing 90 degrees directly to:

```text
0x0071596C
```

after `0x004181A0` returns is too late for calculations that already consumed the source FOV.

It may alter the final projection but leave earlier camera/frustum state inconsistent.

Use the source camera at:

```text
sourceCamera + 0x3C
```

instead.

---

# 20. Suggested configuration

For a proxy DLL, a simple configuration could be:

```ini
[VisualFOV]
Enabled=1
FOV=90
OnlyOverrideVanilla80=1
```

For the first implementation, keep `OnlyOverrideVanilla80=1`.

The generic conversion for another visual FOV value is:

```cpp
int32_t EncodeFov(float degrees)
{
    return static_cast<int32_t>(degrees * 65536.0f);
}
```

For the specific 90-degree implementation:

```cpp
0x005A0000
```

can simply be used directly.

---

# 21. Recommended runtime address handling

Use the executable module base:

```cpp
uintptr_t exeBase =
    reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
```

Then:

```cpp
auto BuildCamera =
    reinterpret_cast<void*>(exeBase + 0x000181A0);

auto CurrentFov =
    reinterpret_cast<int32_t*>(exeBase + 0x007635A0);

auto TargetFov =
    reinterpret_cast<int32_t*>(exeBase + 0x007635A4);
```

This is preferable to hardcoding full virtual addresses in the DLL.

---

# 22. Version validation

Do not install this hook blindly on an unknown DFBHD executable.

The addresses documented here were mapped from one specific executable.

At minimum, validate the executable before enabling the patch.

Recommended options:

1. Check the executable SHA-256.
2. Validate several bytes around the hook target.
3. Use a signature scan for the relevant function/call site.
4. Disable the feature if validation fails.

Expected SHA-256:

```text
6fb91fe9da26a1284638b07041f4d936290823575334b305d7441ca606e4bb19
```

---

# 23. Useful validation points

During development, log these values:

```text
B635A0
B635A4
sourceCamera+0x3C before override
sourceCamera+0x3C during override
0x71596C after camera build
```

In normal first-person view, expected results are:

```text
B635A0:
0x00500000
80 degrees

B635A4:
0x00500000
80 degrees

sourceCamera+0x3C before hook:
0x00500000
80 degrees

sourceCamera+0x3C while calling original:
0x005A0000
90 degrees

0x71596C after 0x004181A0:
0x005A0000
90 degrees
```

After returning from the hook:

```text
sourceCamera+0x3C
```

should have been restored to:

```text
0x00500000
```

---

# 24. LAN validation procedure

A useful two-machine test is:

```text
Machine A:
vanilla DFBHD server

Machine B:
DFBHD client with dinput8.dll visual FOV hook
```

On the client:

```text
Visual camera should be 90 degrees.

B635A0 should remain 80 degrees.

B635A4 should remain 80 degrees.
```

Verify that special modes still behave normally:

```text
normal first-person camera
scope
zoom
binocular/special camera if applicable
map-scripted FOV events
spectator/cinematic states if applicable
```

The first release of the feature should override only an exact normal 80-degree source-camera state.

---

# 25. Expected normal-mode flow with the patch

```text
DFBHD game state

B635A0 = 0x00500000
B635A4 = 0x00500000
        |
        v
0x004B7110
        |
        v
temporaryCamera+0x3C = 0x00500000
        |
        v
dinput8.dll camera hook
        |
        v
temporaryCamera+0x3C = 0x005A0000
        |
        v
0x004181A0
        |
        +--> frustum uses 90 degrees
        |
        +--> render camera uses 90 degrees
        |
        v
0x0071596C = 0x005A0000
        |
        v
0x004FBCC0
        |
        v
90-degree projection

Meanwhile:

B635A0 = 80 degrees
B635A4 = 80 degrees
        |
        v
normal DFBHD network serialization
```

---

# 26. Special-state behavior

The exact-match guard is important:

```cpp
*currentFov == FOV_80
*targetFov == FOV_80
*cameraFov == FOV_80
```

Examples:

### Scope modifies the temporary camera

```text
current = 80
target  = 80
camera  = 64
```

Result:

```text
Do not override.
```

### Special camera uses 20 degrees

```text
camera = 20
```

Result:

```text
Do not override.
```

### Map event changes game FOV to 60

```text
current/target eventually become 60
```

Result:

```text
Do not override.
```

### Normal gameplay

```text
current = 80
target  = 80
camera  = 80
```

Result:

```text
Override temporary render camera to 90.
```

---

# 27. Development logging example

A debug build can log:

```text
[VisualFOV]
caller=004B751F
gameCurrent=00500000
gameTarget=00500000
cameraBefore=00500000
cameraRender=005A0000
globalRenderFov=005A0000
cameraRestored=00500000
```

For a scope or other special state:

```text
[VisualFOV]
caller=004B751F
gameCurrent=00500000
gameTarget=00500000
cameraBefore=00400000
override=SKIPPED
```

This makes it easy to identify any camera mode that requires additional handling later.

---

# 28. Implementation checklist

```text
[ ] Build the proxy DLL as x86.
[ ] Validate the supported DFBHD executable.
[ ] Obtain the EXE module base.
[ ] Install a detour at RVA 0x000181A0.
[ ] Keep a trampoline to the original function.
[ ] Identify the main-render call using return RVA 0x000B751F.
[ ] Read sourceCamera+0x3C.
[ ] Read B635A0.
[ ] Read B635A4.
[ ] Override only exact normal 80-degree state.
[ ] Save sourceCamera+0x3C.
[ ] Temporarily write 0x005A0000.
[ ] Call the original camera builder.
[ ] Restore sourceCamera+0x3C.
[ ] Do not modify B635A0.
[ ] Do not modify B635A4.
[ ] Do not patch packet 0x3F.
[ ] Test normal view.
[ ] Test scopes and zoom.
[ ] Test map FOV events.
[ ] Log 0x71596C for verification.
```

---

# 29. Main addresses summary

```text
0x004181A0
Camera/frustum builder
RVA 0x000181A0

0x004B7110
Main renderer region
RVA 0x000B7110

0x004B7406
Loads B635A0 in normal camera path

0x004B740C
Stores normal FOV into temporaryCamera+0x3C

0x004B751A
Calls 0x004181A0

0x004B751F
Return address after main-render camera-builder call
RVA 0x000B751F

0x004FBCC0
Projection setup/build routine
RVA 0x000FBCC0

0x004F5E9F - 0x004F5EC0
Current/target FOV interpolation region

0x00495EC0
Map FOV event handler

0x00715930
Global render camera structure
RVA 0x00315930

0x0071596C
Global render-camera FOV
0x00715930 + 0x3C
RVA 0x0031596C

0x00B635A0
Current gameplay/network FOV
RVA 0x007635A0

0x00B635A4
Target gameplay/network FOV
RVA 0x007635A4

0x00B63DE0
Default/base FOV
RVA 0x00763DE0
```

---

# 30. Final design rule

The central rule for this patch is:

```text
Do not change the FOV state that DFBHD owns.

Change only the FOV of the temporary render-camera input.
```

For normal gameplay:

```text
DFBHD gameplay/network FOV = 80 degrees
DFBHD temporary render-camera FOV = temporarily changed to 90 degrees
DFBHD render camera/frustum/projection = 90 degrees
```

This provides a clean separation between the original gameplay/network FOV state and the client-side visual camera projection.
