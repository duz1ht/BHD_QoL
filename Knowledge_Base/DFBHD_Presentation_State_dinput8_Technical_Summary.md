# DFBHD Presentation State via `dinput8.dll`
## Technical Implementation Summary

**Target:** Delta Force: Black Hawk Down (`DFBHD.exe`)  
**Method:** local `dinput8.dll` proxy + runtime hooks, with no permanent modification of `DFBHD.exe`  
**Architecture:** 32-bit x86  
**Scope:** introduce a real **Presentation State** between DFBHD's simulation state and its rendering/camera path while preserving the original simulation as the authoritative gameplay state.

---

## 1. Objective

DFBHD already separates parts of its game update from rendering, but it does **not** maintain a clean, independent Presentation State.

The current effective model is approximately:

```text
Input
  |
  v
Fixed game update / simulation
~62.5 Hz
  |
  v
Authoritative player/entity state
  |
  v
Camera preparation / render code
  |
  v
Rendered frame
```

The desired architecture is:

```text
                         +------------------------+
                         |     Raw mouse input    |
                         +-----------+------------+
                                     |
                         +-----------+------------+
                         |                        |
                         v                        v
                Simulation input          Presentation input
                   ~62.5 Hz                 render-rate
                         |                        |
                         v                        |
                +-----------------+              |
                | SimulationState |              |
                +--------+--------+              |
                         | snapshots             |
                         v                        |
                +-----------------+ <------------+
                |PresentationState|
                +--------+--------+
                         |
                         v
                Camera / render transforms
                         |
                         v
                      Renderer
```

The critical rule is:

> **The Presentation State may read Simulation State, but visual smoothing must never write interpolated or predicted values back into the authoritative DFBHD simulation.**

This preserves gameplay, collision, hit detection, weapon logic, networking, and simulation timing while allowing visual state to update at the actual rendered frame rate.

---

# 2. Evidence from the supplied DFBHD build

The supplied executable is:

```text
File: DFBHD(4).EXE
Format: PE32 / i386
Image base: 0x00400000
Relocations: stripped
ASLR flag: not enabled
SHA-256:
6fb91fe9da26a1284638b07041f4d936290823575334b305d7441ca606e4bb19
MD5:
cb3a0cef63dd514230e261b54ff2d769
```

These addresses therefore apply to **this exact executable build**. A DLL intended for distribution should still verify the executable hash or use byte signatures before installing hooks.

## 2.1 Main scheduler

Decompiler function:

```text
sub_4BECA0
VA:  0x004BECA0
RVA: 0x000BECA0
```

The scheduler accumulates elapsed time from `GetTickCount()`, processes 4 ms internal substeps, and only invokes the main game callback when:

```cpp
if ((dword_A0339C & 3) == 0)
{
    gameCallback();
}
```

Four 4 ms substeps produce the approximately **16 ms / 62.5 Hz** game-update cadence.

The same scheduler has separate callback slots after the game update, which is why DFBHD can render more frequently than the main gameplay update.

This distinction is useful, but by itself it is not a true Simulation State / Presentation State architecture.

---

## 2.2 Main gameplay/update callback

Decompiler function:

```text
sub_4B4970
VA:  0x004B4970
RVA: 0x000B4970
```

This function is a practical hook point for detecting completion of an authoritative simulation update.

It:

1. obtains mouse movement,
2. distributes mouse movement over internal game steps,
3. processes game/input logic,
4. advances the game's simulation/update counter,
5. invokes multiple gameplay systems.

A Presentation layer can hook this function and, **after the original function returns**, capture a new authoritative snapshot.

Recommended use:

```cpp
Hook_GameTick(...)
{
    result = Original_GameTick(...);

    CaptureSimulationSnapshot();

    return result;
}
```

Do not modify the simulation snapshot here. Only copy data out.

---

## 2.3 Original mouse polling

Decompiler function:

```text
sub_5678F0
VA:  0x005678F0
RVA: 0x001678F0
```

The function currently:

1. processes mouse messages with `PeekMessageA`,
2. recenters the cursor,
3. computes relative X/Y movement,
4. writes the result to:

```text
Relative X: 0x00F655EC
Relative Y: 0x00F655F0
```

Decompiler global:

```text
qword_F655EC
```

The original code is effectively:

```cpp
LODWORD(qword_F655EC) = dword_F655E0 - 320;
HIDWORD(qword_F655EC) = dword_F655E4 - 240;
```

The game then applies sensitivity/inversion logic to these values.

This is important because the original input is sampled from the simulation-side update path rather than from every rendered frame.

---

## 2.4 DFBHD window handle

The game window is created by:

```text
sub_568DF0
VA: 0x00568DF0
```

The resulting `HWND` is stored at:

```text
0x00F654FC
```

This address was also confirmed directly in the executable disassembly:

```asm
mov ds:0xF654FC, eax
```

The proxy DLL can use this handle to install a WndProc subclass and register Raw Input.

---

## 2.5 Camera construction

Decompiler function:

```text
sub_43B5E0
VA:  0x0043B5E0
RVA: 0x0003B5E0
```

This function builds the camera state used by the rendering path.

The camera output block is:

```text
0x007F2DD8 : camera X
0x007F2DDC : camera Y
0x007F2DE0 : camera Z
0x007F2DE4 : camera yaw
0x007F2DE8 : camera pitch
0x007F2DEC : camera roll
```

For the normal camera path, the function copies position/orientation from the controlled entity and then applies camera-specific behavior.

The function also contains presentation-like effects such as sinusoidal camera movement, shake/bob-style adjustments, and alternate camera modes.

This makes `sub_43B5E0` an excellent interception point:

```text
Original simulation/entity state
          |
          v
     sub_43B5E0
          |
          v
Original DFBHD camera
          |
          +----> Presentation override
                         |
                         v
                final render camera
```

The correct approach is generally to call the original function first, then selectively replace only the fields owned by the new Presentation State.

---

## 2.6 Render/view path

Decompiler function:

```text
sub_4B7110
VA:  0x004B7110
RVA: 0x000B7110
```

This function calls:

```cpp
sub_43B5E0();
```

and immediately reads:

```text
dword_7F2DD8
dword_7F2DDC
dword_7F2DE0
dword_7F2DE4
dword_7F2DE8
dword_7F2DEC
```

into its render/view working state.

Therefore:

- `sub_4B4970` is a useful **simulation snapshot boundary**.
- `sub_4B7110` is a useful **presentation frame boundary**.
- `sub_43B5E0` is a useful **camera composition boundary**.

This is enough to implement a clean camera Presentation State without rewriting DFBHD's game simulation.

---

# 3. Loading through `dinput8.dll`

DFBHD imports:

```text
DINPUT8.dll
    DirectInput8Create
```

The decompiled DirectInput initialization path is:

```text
sub_56B4D0
VA: 0x0056B4D0
```

and contains:

```cpp
DirectInput8Create(hinst, 0x800, ..., &ppvOut, 0);
```

This makes a local proxy DLL a natural injection mechanism.

Directory layout:

```text
DFBHD.exe
dinput8.dll        <- custom proxy/patch DLL
```

Windows loads the application-local DLL before the system copy.

The proxy must then load the real system `dinput8.dll` and forward calls to it.

---

# 4. Safe proxy initialization

Do not perform complex hook installation directly inside `DllMain`.

A safer sequence is:

```text
DFBHD starts
    |
    v
local dinput8.dll loaded
    |
    v
DFBHD calls DirectInput8Create()
    |
    v
proxy DirectInput8Create()
    |
    +--> Initialize patch once
    |
    +--> forward to real system dinput8.dll
```

Example skeleton:

```cpp
using DirectInput8CreateFn =
    HRESULT (WINAPI*)(
        HINSTANCE,
        DWORD,
        REFIID,
        LPVOID*,
        LPUNKNOWN);

static HMODULE g_realDInput8 = nullptr;
static DirectInput8CreateFn g_realDirectInput8Create = nullptr;
static INIT_ONCE g_initOnce = INIT_ONCE_STATIC_INIT;

static bool LoadRealDInput8()
{
    wchar_t systemDir[MAX_PATH];
    if (!GetSystemDirectoryW(systemDir, MAX_PATH))
        return false;

    std::wstring path = systemDir;
    path += L"\\dinput8.dll";

    g_realDInput8 = LoadLibraryW(path.c_str());
    if (!g_realDInput8)
        return false;

    g_realDirectInput8Create =
        reinterpret_cast<DirectInput8CreateFn>(
            GetProcAddress(g_realDInput8, "DirectInput8Create"));

    return g_realDirectInput8Create != nullptr;
}

static BOOL CALLBACK InitializeOnce(
    PINIT_ONCE,
    PVOID,
    PVOID*)
{
    if (!LoadRealDInput8())
        return FALSE;

    if (!ValidateDFBHDExecutable())
        return TRUE; // forward DirectInput normally, but install no patches

    InstallPresentationHooks();

    return TRUE;
}

extern "C"
HRESULT WINAPI DirectInput8Create(
    HINSTANCE hinst,
    DWORD version,
    REFIID riid,
    LPVOID* out,
    LPUNKNOWN outer)
{
    InitOnceExecuteOnce(
        &g_initOnce,
        InitializeOnce,
        nullptr,
        nullptr);

    return g_realDirectInput8Create(
        hinst,
        version,
        riid,
        out,
        outer);
}
```

For this DFBHD executable, `DirectInput8Create` is the only imported DINPUT8 symbol observed in the PE import table. A general-purpose proxy can still forward the normal additional system `dinput8.dll` exports for robustness.

---

# 5. Address resolution

For this exact build:

```cpp
constexpr uintptr_t kImageBase = 0x00400000;

constexpr uintptr_t RVA_GameScheduler = 0x000BECA0; // 0x004BECA0
constexpr uintptr_t RVA_GameTick      = 0x000B4970; // 0x004B4970
constexpr uintptr_t RVA_RenderView    = 0x000B7110; // 0x004B7110
constexpr uintptr_t RVA_BuildCamera   = 0x0003B5E0; // 0x0043B5E0
constexpr uintptr_t RVA_PollMouse     = 0x001678F0; // 0x005678F0

constexpr uintptr_t VA_GameHwnd       = 0x00F654FC;

constexpr uintptr_t VA_MouseDX        = 0x00F655EC;
constexpr uintptr_t VA_MouseDY        = 0x00F655F0;

constexpr uintptr_t VA_CameraX        = 0x007F2DD8;
constexpr uintptr_t VA_CameraY        = 0x007F2DDC;
constexpr uintptr_t VA_CameraZ        = 0x007F2DE0;
constexpr uintptr_t VA_CameraYaw      = 0x007F2DE4;
constexpr uintptr_t VA_CameraPitch    = 0x007F2DE8;
constexpr uintptr_t VA_CameraRoll     = 0x007F2DEC;
```

Because this executable has relocation data stripped and uses image base `0x00400000`, these virtual addresses are expected to be fixed for this build.

Nevertheless, production code should do one of the following:

1. verify the executable SHA-256 before using fixed addresses, or
2. pattern-scan each hook site and validate surrounding instructions.

Never silently apply this patch to an unknown DFBHD build.

---

# 6. Hook implementation

The DLL can use:

- MinHook,
- another x86 detour library,
- or a small custom 5-byte trampoline.

For a custom x86 trampoline:

```text
target:
    original bytes...

patched target:
    E9 xx xx xx xx        ; JMP hook
```

The trampoline contains:

```text
copied original instructions
JMP target + copied_length
```

Requirements:

1. copy complete x86 instructions until at least 5 bytes are available,
2. preserve relative branches/calls correctly,
3. call `VirtualProtect` before modifying executable memory,
4. call `FlushInstructionCache` after patching,
5. preserve the original calling convention and registers,
6. restore hooks on shutdown only if it is safe to do so.

Because Hex-Rays identifies some functions with custom register arguments (`__usercall`), do **not** assume every target is a normal `__cdecl`.

For difficult `__usercall` functions, use a naked assembly bridge or hook a nearby call site with a known register layout.

---

# 7. Core Presentation State

A practical first implementation:

```cpp
struct SimTransform
{
    int32_t x;
    int32_t y;
    int32_t z;

    uint32_t yaw;
    uint32_t pitch;
    uint32_t roll;
};

struct SimulationSnapshot
{
    SimTransform localPlayer;

    uint64_t tickIndex;
    int64_t qpcTime;

    bool valid;
};

struct CameraPresentation
{
    double x;
    double y;
    double z;

    uint32_t yaw;
    uint32_t pitch;
    uint32_t roll;

    // render-only terms
    int32_t visualYawOffset;
    int32_t visualPitchOffset;

    double recoilYaw;
    double recoilPitch;

    double bobX;
    double bobY;
    double bobZ;
};

struct PresentationState
{
    SimulationSnapshot previous;
    SimulationSnapshot current;

    CameraPresentation camera;

    int64_t lastFrameQpc;
    double frameDt;

    bool initialized;
    bool enabled;
};
```

The important architectural difference is that the Presentation State owns the transform that will be rendered.

The original player/entity transform remains authoritative.

---

# 8. What belongs in Presentation State

The following data should be presentation-owned or have a presentation copy.

## 8.1 Camera

Presentation-owned:

```text
render camera position
render camera yaw
render camera pitch
render camera roll
camera interpolation
visual recoil
camera bob
camera sway
camera shake
stance transition offset
landing visual impulse
view kick
```

The simulation may still contain authoritative aim and gameplay recoil. Presentation only creates the rendered result.

---

## 8.2 Local player render transform

Presentation copy:

```text
rendered player position
rendered body orientation
rendered stance transition
rendered skeleton pose
rendered attachment transforms
```

The authoritative collision capsule/entity must remain in Simulation State.

---

## 8.3 Other entities

Presentation copy:

```text
render position
render orientation
interpolated animation pose
render-only attachment transform
render-only vehicle transform
```

Do not overwrite the entity's simulation transform.

The renderer should eventually consume a scratch/render transform rather than the live gameplay transform.

---

## 8.4 First-person weapon/viewmodel

Presentation-owned:

```text
viewmodel position
viewmodel rotation
weapon bob
weapon sway
visual recoil
visual kick
raise/lower transition
ADS visual transition
visual muzzle displacement
```

The actual weapon state remains simulation-owned.

---

## 8.5 Animation presentation

Presentation-owned:

```text
interpolated skeleton pose
visual animation phase
render-time blending
bone smoothing
viewmodel animation interpolation
```

Simulation still owns gameplay-important animation events when those affect hitboxes, weapon timing, stance, or actions.

---

## 8.6 Visual events

Good candidates:

```text
muzzle flash rendering
tracer rendering
shell ejection
particle position
temporary light effects
camera shake
screen effects
damage flash
visual-only debris
```

The simulation should generate events; Presentation State renders them.

---

# 9. What must remain in Simulation State

Do **not** move these into Presentation State:

```text
authoritative player position
authoritative entity position
velocity used by physics
collision
gravity
grounding
stance used by collision/gameplay
hit detection
damage
health
ammo
reload state
fire cooldown
weapon spread used for shot calculation
gameplay recoil affecting shot direction
projectile simulation
AI state
gameplay RNG
network-authoritative state
entity creation/destruction
mission logic
trigger state
vehicle physics
```

Presentation may mirror these values, but it must not become their authority.

---

# 10. Capturing simulation snapshots

Hook `sub_4B4970`.

The recommended sequence is:

```cpp
int Hook_GameTick(...)
{
    int result = Original_GameTick(...);

    // Original DFBHD has now advanced its authoritative state.
    CaptureSimulationSnapshot();

    return result;
}
```

Snapshot rotation:

```cpp
void CaptureSimulationSnapshot()
{
    g_present.previous = g_present.current;

    g_present.current.localPlayer =
        ReadAuthoritativePlayerTransform();

    QueryPerformanceCounter(
        reinterpret_cast<LARGE_INTEGER*>(
            &g_present.current.qpcTime));

    ++g_present.current.tickIndex;
    g_present.current.valid = true;
}
```

The DLL should copy only fields it has positively identified.

Do not infer undocumented structure members and then write to them.

---

# 11. Render-rate Presentation update

Hook the render/view boundary at `sub_4B7110`.

Conceptually:

```cpp
int Hook_RenderView(...)
{
    UpdatePresentationFrame();

    return Original_RenderView(...);
}
```

`UpdatePresentationFrame()` runs once for every view/render update rather than once per simulation tick.

It should:

1. read high-resolution current time,
2. calculate render-frame delta,
3. consume presentation-side mouse movement,
4. update presentation camera yaw/pitch,
5. compute smoothing/interpolation state,
6. update render-only recoil/bob/sway,
7. leave Simulation State untouched.

Use `QueryPerformanceCounter`, not `GetTickCount`, for Presentation timing.

---

# 12. Mouse input at render rate

This is the most important part for modern camera response.

The existing DFBHD poll path stores relative mouse movement only when the original game polls it.

A new Presentation State should receive mouse input independently of the ~62.5 Hz simulation cadence.

## Recommended design: Raw Input as a shared event source

Register the DFBHD game window for Raw Input:

```cpp
RAWINPUTDEVICE rid{};
rid.usUsagePage = 0x01;
rid.usUsage     = 0x02; // mouse
rid.dwFlags     = 0;
rid.hwndTarget  = gameHwnd;

RegisterRawInputDevices(
    &rid,
    1,
    sizeof(rid));
```

Subclass the DFBHD `WndProc` and process `WM_INPUT`.

Use 64-bit cumulative counters:

```cpp
struct RawMouseAccumulator
{
    std::atomic<int64_t> totalX{0};
    std::atomic<int64_t> totalY{0};

    int64_t simCursorX = 0;
    int64_t simCursorY = 0;

    int64_t presentCursorX = 0;
    int64_t presentCursorY = 0;
};
```

On `WM_INPUT`:

```cpp
raw.totalX += dx;
raw.totalY += dy;
```

This creates **one physical input stream with two independent consumers**.

### Simulation consumer

At a simulation tick:

```cpp
simDX = totalX - simCursorX;
simDY = totalY - simCursorY;

simCursorX = totalX;
simCursorY = totalY;
```

### Presentation consumer

At every rendered frame:

```cpp
presentDX = totalX - presentCursorX;
presentDY = totalY - presentCursorY;

presentCursorX = totalX;
presentCursorY = totalY;
```

This is preferable to a destructive queue because Presentation and Simulation do not steal samples from one another.

---

# 13. Integration with `sub_5678F0`

There are two implementation modes.

## Mode A: lowest-risk initial implementation

Leave `sub_5678F0` unchanged.

Use Raw Input only as a high-rate side channel for Presentation camera movement.

Advantages:

```text
minimal gameplay risk
original DFBHD simulation input remains untouched
easy to disable/fallback
```

Disadvantage:

```text
presentation input and original simulation input are obtained through
different input paths and may require reconciliation
```

This is the recommended first development stage.

---

## Mode B: unified Raw Input source

Detour `sub_5678F0` and provide its `qword_F655EC` values from the same cumulative Raw Input counters used by Presentation.

Conceptually:

```cpp
int Hook_PollMouse()
{
    int32_t dx = ConsumeSimulationRawX();
    int32_t dy = ConsumeSimulationRawY();

    *reinterpret_cast<int32_t*>(0x00F655EC) = dx;
    *reinterpret_cast<int32_t*>(0x00F655F0) = dy;

    return dy;
}
```

Now:

```text
Raw Input
   |
   +--> Presentation consumer: every rendered frame
   |
   +--> Simulation consumer: original DFBHD tick
```

This provides the cleanest long-term architecture.

However, it changes the original cursor-warp/mouse-message input mechanism and therefore must be treated as a separate compatibility option.

---

# 14. Mouse sensitivity and angle ownership

Do not interpolate local mouse look.

Mouse movement should immediately update Presentation camera orientation:

```cpp
presentation.camera.yaw +=
    ConvertMouseXToDFBHDAngle(dx);

presentation.camera.pitch +=
    ConvertMouseYToDFBHDAngle(dy);
```

The authoritative simulation continues consuming its own accumulated movement at its fixed update cadence.

The two states therefore become:

```text
Simulation aim:
updated at DFBHD's original game-update rate

Presentation aim:
updated whenever new mouse input is available / rendered
```

This removes the visible 62.5 Hz stepping from mouse camera rotation without increasing DFBHD physics/gameplay rate.

---

# 15. DFBHD angle representation

The camera code indicates that DFBHD uses a 32-bit full-turn angular representation.

The conversion constant used around `atan2()` corresponds to:

```text
2*pi radians = 2^32 angle units
```

Useful landmarks:

```text
0 degrees   = 0x00000000
90 degrees  = 0x40000000
180 degrees = 0x80000000
270 degrees = 0xC0000000
360 degrees = wraps to 0x00000000
```

Angle interpolation must use the shortest wrapped path.

Example:

```cpp
uint32_t LerpAngle32(
    uint32_t a,
    uint32_t b,
    double t)
{
    int32_t delta =
        static_cast<int32_t>(b - a);

    int64_t step =
        static_cast<int64_t>(
            static_cast<double>(delta) * t);

    return a + static_cast<uint32_t>(step);
}
```

Never linearly interpolate unsigned yaw directly across the `0xFFFFFFFF -> 0` wrap boundary.

---

# 16. Camera hook strategy

The safest initial camera modification is:

1. call original `sub_43B5E0`,
2. let DFBHD construct its normal camera,
3. retain original DFBHD camera X/Y/Z and roll,
4. replace only yaw/pitch with Presentation yaw/pitch.

Example:

```cpp
int Hook_BuildCamera()
{
    int result = Original_BuildCamera();

    if (!PresentationCameraActive())
        return result;

    auto* yaw =
        reinterpret_cast<uint32_t*>(0x007F2DE4);

    auto* pitch =
        reinterpret_cast<uint32_t*>(0x007F2DE8);

    *yaw   = g_present.camera.yaw;
    *pitch = g_present.camera.pitch;

    return result;
}
```

This produces the first major improvement while preserving:

```text
original camera position
original camera modes
original collision/third-person camera behavior
original bob/shake calculations
original roll behavior
```

It is a much lower-risk first step than replacing the whole camera function.

---

# 17. Preserving original DFBHD bob, shake, and offsets

Once positional smoothing is added, do not simply discard the output of `sub_43B5E0`.

A useful composition model is:

```text
Original DFBHD camera
    =
Simulation base transform
    +
DFBHD camera presentation offsets
```

The new camera should become:

```text
New camera
    =
Smoothed/predicted presentation base transform
    +
DFBHD camera presentation offsets
    +
new render-rate mouse orientation
```

In conceptual form:

```cpp
original = CameraProducedByDFBHD();

dfbhdOffset =
    original - simulationBaseCamera;

presented =
    smoothedBaseCamera + dfbhdOffset;

presented.yaw =
    presentationMouseYaw
    + originalVisualYawOffset;

presented.pitch =
    presentationMousePitch
    + originalVisualPitchOffset;
```

The exact decomposition of all DFBHD camera offsets requires validation for each camera mode.

For this reason, yaw/pitch-only presentation should be implemented before positional replacement.

---

# 18. Position interpolation

Maintain:

```text
previous simulation snapshot
current simulation snapshot
last simulation tick time
current render time
```

Classic interpolation:

```cpp
alpha =
    (renderTime - currentTickTime)
    / fixedTickDuration;
```

For a stable delayed interpolation buffer, render between known snapshots:

```cpp
renderPosition =
    Lerp(previous.position,
         current.position,
         alpha);
```

This is smooth but introduces roughly one simulation tick of visual delay.

That is acceptable for many world entities but is not ideal for the local first-person camera.

---

# 19. Local-player translation

For the local camera, three choices exist.

## Option 1: no positional interpolation initially

Use authoritative simulation X/Y/Z directly but free yaw/pitch from the tick.

This is the safest first release.

It removes mouse-look stepping without affecting movement behavior.

## Option 2: extrapolate local render position

Use:

```cpp
predicted =
    current.position
    + (current.position - previous.position)
    * alpha;
```

Advantages:

```text
no one-tick interpolation delay
smooth local movement
```

Disadvantages:

```text
can overshoot at collision
needs correction when velocity changes abruptly
```

Any prediction error must be corrected visually, never by modifying Simulation State.

## Option 3: velocity-based prediction

If DFBHD's authoritative velocity vector is positively identified:

```cpp
renderPosition =
    current.position
    + current.velocity
    * timeSinceTick;
```

This is preferable to estimating velocity from two positions.

Do not implement this until the actual velocity fields are verified.

---

# 20. Remote/world entity interpolation

For non-local entities, standard delayed interpolation is usually preferable:

```text
snapshot N
snapshot N+1
     |
     v
render intermediate transforms
```

Example:

```cpp
renderPos =
    Lerp(prev.pos, curr.pos, alpha);

renderYaw =
    LerpAngle32(prev.yaw, curr.yaw, alpha);
```

Important:

> Do not overwrite the entity's live DFBHD transform with the interpolated transform.

Instead, intercept the transform immediately before the model is submitted for rendering and provide a temporary render transform.

The supplied decompiled evidence clearly identifies the camera boundary, but it does **not yet establish one universal render-transform function for every entity type**. That render submission point should be reverse-engineered separately before world-entity interpolation is implemented.

---

# 21. Animation Presentation State

Once entity render transforms are separated, animation should follow the same rule.

Simulation owns:

```text
animation state that affects gameplay
action state
stance
weapon timing
hitbox-relevant state
```

Presentation owns:

```text
render pose
bone interpolation
pose blending
frame interpolation
visual transition smoothing
```

For two skeleton snapshots:

```cpp
renderBonePosition =
    Lerp(prevBonePosition,
         currBonePosition,
         alpha);

renderBoneRotation =
    Slerp(prevBoneRotation,
          currBoneRotation,
          alpha);
```

If DFBHD stores bone orientation in its own fixed-angle representation, convert to a stable intermediate rotation representation for interpolation, then convert back only in the render scratch state.

---

# 22. Presentation events

A useful event bridge is:

```cpp
enum class PresentationEventType
{
    Fire,
    Reload,
    Land,
    Damage,
    Explosion,
    MuzzleFlash,
    ShellEject
};
```

Simulation emits an event once:

```cpp
PresentationEvent {
    type,
    entityId,
    simulationTick,
    position,
    orientation,
    seedOrVisualVariant
}
```

Presentation consumes the event and creates visual state.

This prevents a 360 FPS renderer from accidentally spawning a muzzle flash or shell 5-6 times for one 62.5 Hz simulation event.

---

# 23. Threading

The safest assumption is that the DFBHD simulation/render callbacks are primarily executed on the game's main thread.

Do not introduce an independent Presentation thread unless necessary.

Recommended:

```text
WM_INPUT:
small atomic accumulation only

Simulation hook:
snapshot copy

Render hook:
PresentationState update

Camera hook:
final camera composition
```

Use atomics only for data crossing the window-input callback and rendering/update path.

Avoid locks in the render hot path where possible.

---

# 24. Presentation frame timing

Use:

```cpp
QueryPerformanceFrequency();
QueryPerformanceCounter();
```

Store:

```cpp
double qpcSeconds =
    double(counter.QuadPart)
    / double(frequency.QuadPart);
```

Clamp abnormal frame deltas:

```cpp
frameDt = std::clamp(
    now - previousFrame,
    0.0,
    0.100);
```

Presentation frame rate must not change simulation time.

Never call DFBHD simulation functions multiple times simply because more frames are being rendered.

---

# 25. Pause, menus, loading, and camera mode changes

Presentation state must reset/rebase when:

```text
entering a mission
leaving a mission
respawning
switching controlled entity
switching camera modes
entering a vehicle
leaving a vehicle
opening some modal game states
alt-tabbing / losing focus
teleporting
large position discontinuity occurs
```

Rebase rule:

```cpp
previous = current = newSimulationSnapshot;

presentation.camera =
    current authoritative camera;

clear mouse presentation offsets;
clear interpolation history;
clear prediction error;
```

Without rebasing, interpolation can incorrectly sweep the camera/entity across a teleport or respawn.

---

# 26. Third-person and special camera modes

`sub_43B5E0` handles more than the normal first-person camera.

Therefore the Presentation hook must detect the active camera mode.

Initial implementation recommendation:

```text
first person:
    presentation yaw/pitch enabled

third person / alternate camera:
    original DFBHD camera untouched

cinematic / scripted camera:
    original DFBHD camera untouched
```

After each mode is understood, it can receive its own presentation policy.

Do not globally overwrite the six camera globals for every mode in the first implementation.

---

# 27. Recommended internal architecture

Suggested source layout:

```text
dinput8/
|
|-- proxy/
|   |-- dinput8_proxy.cpp
|   `-- dinput8.def
|
|-- patch/
|   |-- dfbhd_addresses.h
|   |-- executable_validation.cpp
|   |-- hooks.cpp
|   `-- trampoline_x86.cpp
|
|-- presentation/
|   |-- presentation_state.h
|   |-- presentation_state.cpp
|   |-- simulation_snapshot.cpp
|   |-- camera_presentation.cpp
|   |-- entity_presentation.cpp
|   `-- animation_presentation.cpp
|
|-- input/
|   |-- raw_input.cpp
|   `-- wndproc_hook.cpp
|
`-- config/
    `-- config.cpp
```

---

# 28. Suggested configuration

```ini
[Presentation]
Enabled=1

CameraPresentation=1
RenderRateMouse=1

CameraPositionSmoothing=0
EntityInterpolation=0
AnimationInterpolation=0

PreserveOriginalCameraBob=1
PreserveOriginalCameraShake=1

UseRawInputForSimulation=0

DebugOverlay=0
```

This permits gradual activation.

---

# 29. Implementation phases

## Phase 0: compatibility shell

Implement:

```text
dinput8 proxy
real DirectInput forwarding
hash/version verification
logging
safe hook enable/disable
```

No gameplay modifications yet.

---

## Phase 1: Presentation State foundation

Implement:

```text
PresentationState structure
QPC render timing
sub_4B4970 tick hook
previous/current snapshots
sub_4B7110 render boundary hook
```

At this stage, render output can remain unchanged.

Goal: prove that simulation ticks and rendered frames are tracked independently.

---

## Phase 2: render-rate camera mouse

Implement:

```text
Raw Input
WndProc hook
presentation mouse accumulator
presentation yaw/pitch
sub_43B5E0 post-hook
```

Initially replace only:

```text
camera yaw
camera pitch
```

Keep DFBHD camera position and roll unchanged.

This should be the first visible Presentation-State feature.

---

## Phase 3: unified input source

Optional:

```text
replace sub_5678F0 input source
feed both Simulation and Presentation from cumulative Raw Input
```

This gives a single physical mouse input source with separate consumers.

Verify:

```text
same total movement
same sensitivity
no double counting
no lost counts
no drift
```

---

## Phase 4: local camera translation smoothing

Add:

```text
local render position
prediction or smoothing
correction after collision
teleport/reset detection
```

Keep the authoritative player transform untouched.

---

## Phase 5: world entity interpolation

Reverse-engineer the final model transform submission path.

Then create per-entity:

```text
previous transform
current transform
render transform
```

Render the scratch transform only.

---

## Phase 6: animation presentation

Add:

```text
skeleton pose interpolation
visual animation blending
viewmodel smoothing
```

Again, no write-back into gameplay state.

---

## Phase 7: presentation effects

Move visual-only effects behind the Presentation layer:

```text
visual recoil
bob
sway
muzzle flash
tracers
shells
particles
camera impulses
screen effects
```

Use simulation events as triggers.

---

# 30. Debugging instrumentation

Add an optional overlay/log showing:

```text
Simulation tick index
Simulation Hz
Render frame index
Render FPS
QPC frame dt
last simulation tick age

Sim position
Presented position

Sim yaw/pitch
Presented yaw/pitch

Raw mouse total X/Y
Simulation-consumed X/Y
Presentation-consumed X/Y

Camera mode
Presentation enabled/disabled
```

This is extremely useful for detecting accidental coupling.

Example test:

```text
Render: 360 FPS
Simulation: ~62.5 Hz

Expected:
Presented camera yaw can change on consecutive render frames.

Simulation yaw changes only when the DFBHD game tick updates.
```

---

# 31. Invariants

The implementation should continuously obey these invariants.

## Invariant 1

```text
Presentation code never advances simulation.
```

## Invariant 2

```text
Presentation transforms never overwrite authoritative entity transforms.
```

## Invariant 3

```text
Rendering can be disabled without changing gameplay results.
```

## Invariant 4

```text
Changing FPS must not change movement speed, fire rate, physics, or game logic.
```

## Invariant 5

```text
Presentation interpolation is allowed to be wrong for a frame.
Simulation must remain correct.
```

## Invariant 6

```text
Local mouse camera response must not wait for interpolation.
```

---

# 32. Failure cases to avoid

## Writing interpolated position back into DFBHD player memory

Wrong:

```cpp
player->x = interpolatedX;
```

This turns a visual modification into a gameplay modification.

Correct:

```cpp
presentation.renderX = interpolatedX;
```

and only the renderer/camera receives it.

---

## Interpolating local mouse orientation

Wrong:

```cpp
cameraYaw =
    Lerp(oldYaw, newYaw, alpha);
```

when `newYaw` is the latest local mouse orientation.

This adds unnecessary mouse latency.

Local Presentation yaw/pitch should respond directly to input.

---

## Polling simulation more frequently

Do not solve presentation stepping by calling the DFBHD tick function at 240/360 Hz.

That would change:

```text
physics
timers
movement
weapons
AI
network behavior
gameplay
```

The purpose of Presentation State is precisely to avoid doing this.

---

## Using one mutable transform for both systems

Do not create:

```cpp
player.position
```

and alternately fill it with simulation and interpolated values.

Maintain:

```text
SimulationState.position
PresentationState.renderPosition
```

as separate concepts.

---

# 33. Minimal viable Presentation State

The smallest useful implementation is:

```text
1. dinput8.dll proxy loads automatically.
2. Validate exact DFBHD.exe build.
3. Hook sub_4B4970 and record simulation tick timing.
4. Hook sub_4B7110 and run one Presentation update per rendered view.
5. Register Raw Input.
6. Accumulate mouse deltas independently.
7. Maintain presentationCameraYaw / presentationCameraPitch.
8. Hook sub_43B5E0.
9. Call original sub_43B5E0.
10. Override only camera yaw/pitch before sub_4B7110 consumes them.
11. Never write presentation orientation back into player/entity state.
```

Result:

```text
DFBHD simulation:
unchanged, approximately 62.5 Hz

DFBHD camera rotation:
can update at rendered-frame/input rate

DFBHD gameplay:
still uses original authoritative simulation
```

This establishes the architectural boundary first.

Everything else can then be migrated into Presentation State incrementally.

---

# 34. Recommended first implementation

For this specific DFBHD project, the first production-quality milestone should **not** attempt to move every visual system at once.

Implement:

```text
SimulationSnapshot
PresentationState
render-rate timing
render-rate mouse
presentation camera yaw/pitch
original DFBHD camera X/Y/Z
original DFBHD camera bob/shake
original simulation unchanged
```

Once this is stable, add:

```text
local camera translation
world transform interpolation
animation interpolation
viewmodel presentation
visual recoil/effects
```

This minimizes the chance of accidentally changing original BHD gameplay behavior while establishing a proper modern Simulation State / Presentation State boundary.

---

# 35. Final target architecture

```text
                        WINDOWS INPUT
                             |
                             v
                       Raw Input stream
                             |
                +------------+------------+
                |                         |
                v                         v
       Simulation consumer       Presentation consumer
           ~62.5 Hz                  frame-rate
                |                         |
                v                         |
       +------------------+               |
       | Simulation State |               |
       | authoritative    |               |
       +--------+---------+               |
                |                         |
                | snapshots/events        |
                v                         |
       +------------------+ <-------------+
       |Presentation State|
       | render-only      |
       +--------+---------+
                |
        +-------+--------+----------------+
        |                |                |
        v                v                v
      Camera        Entity transforms    Viewmodel
        |                |                |
        +----------------+----------------+
                         |
                         v
                    DFBHD renderer
```

The central principle is:

> **DFBHD continues deciding what is true. Presentation State decides how that truth is displayed between simulation updates.**

---

# 36. Source-derived points vs implementation proposal

## Directly supported by the supplied executable/decompilation

The supplied build supports the following conclusions:

```text
DFBHD is PE32 x86.
Image base is 0x00400000.
Relocations are stripped.
DFBHD imports DirectInput8Create from DINPUT8.dll.
sub_56B4D0 calls DirectInput8Create.
sub_4BECA0 is the relevant scheduler containing the fixed/substep callback pattern.
sub_4B4970 consumes/distributes mouse movement in the game-update path.
sub_5678F0 generates relative mouse X/Y and stores them at 0xF655EC/0xF655F0.
hWnd is stored at 0xF654FC.
sub_43B5E0 constructs the camera output block.
sub_4B7110 calls sub_43B5E0 and consumes the resulting camera values.
```

## Proposed engineering architecture

The following are implementation recommendations, not existing DFBHD systems:

```text
PresentationState structure
Raw Input cumulative dual-consumer design
render-rate camera yaw/pitch
snapshot interpolation
local render prediction
entity render scratch transforms
animation pose interpolation
event bridge
QPC-based Presentation timing
```

These systems should be added by the proxy DLL without changing the authority of DFBHD's original simulation.
