# DFBHD Free-Rate `PollMouseInput()` Experiment
## Narrow implementation handoff for the `dinput8.dll`

## Scope

This experiment has one goal only:

> Execute the game's `PollMouseInput()` path once per rendered frame instead of only once per ~62.5 Hz logic tick, while preserving the original logic input stream.

Do **not** implement camera prediction, matrix prediction, interpolation, simulation-rate changes, or `UpdateWorldCamera()` changes in this patch.

The purpose is to create a clean high-rate mouse-poll stream synchronized to render FPS so that a later camera experiment can consume a fresh mouse delta on every rendered frame.

---

# Important limitation

Calling `PollMouseInput()` more often does **not by itself** make the camera update more often.

In the original game, `PollMouseInput()` fills the game's relative mouse values, and the logic path later performs scaling/action processing.

Therefore this patch should be considered infrastructure:

```text
WM_INPUT at mouse report rate
        |
        v
independent pending mouse streams
        |
        +----------------------------+
        |                            |
        v                            v
logic PollMouseInput           render PollMouseInput
~62.5 Hz                       render FPS
        |                            |
        v                            v
original gameplay input        fresh per-render mouse delta
```

After this patch, a later camera patch can consume the per-render sample.

Do not claim that this patch alone produces a high-rate camera.

---

# Why the current DLL needs a change

The current Raw Input backend has one accumulator:

```cpp
g_accumX
g_accumY
```

`WM_INPUT` adds relative movement to those counters.

`RawPollMouseInput()` currently drains them:

```cpp
const LONG x = InterlockedExchange(&g_accumX, 0);
const LONG y = InterlockedExchange(&g_accumY, 0);
```

and writes:

```text
0x00F655EC = relative X
0x00F655F0 = relative Y
```

If the render loop simply calls the current `RawPollMouseInput()` at 240 FPS, the render calls will empty the accumulator before the 62.5 Hz logic tick sees it.

That would starve or fragment the real gameplay input.

Therefore a free-rate render poll requires **two independent Raw Input accumulators**.

---

# Existing useful behavior

The current Raw Input implementation already updates the virtual in-game cursor position directly from `WM_INPUT`:

```text
0x00F655E0 = cursor X
0x00F655E4 = cursor Y
```

through `UpdateVirtualCursor()`.

So the main value that remains logic-poll limited is the relative movement stream:

```text
0x00F655EC = relative X
0x00F655F0 = relative Y
```

This experiment focuses on that relative stream.

---

# Relevant addresses

For the currently supported `DFBHD.EXE`:

```text
PollMouseInput
0x005678F0

logic/input update
sub_4B4970
0x004B4970

logic PollMouseInput CALL site
0x004B49CB

CalculateCameraPositions
0x0043B5E0

render CalculateCameraPositions CALL site
0x004B7297
```

The current DLL already detours:

```text
0x005678F0
```

to:

```cpp
RawPollMouseInput()
```

Keep that detour.

The new render-rate trigger should be installed at:

```text
0x004B7297
```

because this call occurs once per rendered camera frame.

Original bytes at that call site:

```text
E8 44 43 F8 FF
```

which is:

```asm
CALL 0x0043B5E0
```

Replace that CALL with a wrapper.

---

# Target behavior

With a 1000 Hz mouse and a 240 FPS render rate:

```text
WM_INPUT packets:         ~1000 / sec
logic PollMouseInput:       ~62.5 / sec
render PollMouseInput:     ~240 / sec
```

Both poll streams must see the full physical movement relevant to their own cadence.

A render poll must never remove movement from the logic stream.

---

# Step 1: split the Raw Input accumulators

Replace the single pending stream with two independent streams.

Suggested state:

```cpp
volatile LONG g_logicAccumX = 0;
volatile LONG g_logicAccumY = 0;

volatile LONG g_renderAccumX = 0;
volatile LONG g_renderAccumY = 0;
```

When an accepted relative `WM_INPUT` packet arrives:

```cpp
InterlockedExchangeAdd(&g_logicAccumX, mouse.lLastX);
InterlockedExchangeAdd(&g_logicAccumY, mouse.lLastY);

InterlockedExchangeAdd(&g_renderAccumX, mouse.lLastX);
InterlockedExchangeAdd(&g_renderAccumY, mouse.lLastY);

UpdateVirtualCursor(mouse.lLastX, mouse.lLastY);
```

The two accumulators receive the exact same physical packets.

They are consumed independently.

---

# Step 2: distinguish logic polls from render polls

`PollMouseInput()` has no arguments, so the detour needs a small context mechanism.

Use thread-local render-poll context:

```cpp
thread_local bool g_renderPollContext = false;
```

Provide a scoped helper:

```cpp
class RenderPollScope
{
public:
    RenderPollScope()
    {
        g_renderPollContext = true;
    }

    ~RenderPollScope()
    {
        g_renderPollContext = false;
    }
};
```

Do not use a process-global boolean if avoidable.

A thread-local flag prevents an unrelated call on another thread from being misclassified.

---

# Step 3: refactor `RawPollMouseInput()`

The existing detour should decide which accumulator to drain.

Conceptually:

```cpp
extern "C" void __cdecl RawPollMouseInput()
{
    EnsureInitialized();

    if (BackendIsNotActive())
    {
        HandleInactiveBackendExactlyAsBefore();
        return;
    }

    *reinterpret_cast<volatile LONG*>(kMouseState) =
        static_cast<LONG>(CurrentState());

    LONG x = 0;
    LONG y = 0;

    if (g_renderPollContext)
    {
        x = InterlockedExchange(&g_renderAccumX, 0);
        y = InterlockedExchange(&g_renderAccumY, 0);

        InterlockedExchange(&g_lastRenderDeltaX, x);
        InterlockedExchange(&g_lastRenderDeltaY, y);
        InterlockedIncrement(&g_renderPollCount);
    }
    else
    {
        x = InterlockedExchange(&g_logicAccumX, 0);
        y = InterlockedExchange(&g_logicAccumY, 0);

        InterlockedIncrement(&g_logicPollCount);
    }

    *reinterpret_cast<volatile LONG*>(kRelativeX) = x;
    *reinterpret_cast<volatile LONG*>(kRelativeY) = y;
}
```

Keep the current statistics/focus logic, but separate the counters for:

```text
logic polls
render polls
```

---

# Step 4: expose the last render sample

Add to `raw_input.h`:

```cpp
namespace raw_input {

struct RenderMouseDelta
{
    LONG x;
    LONG y;
};

RenderMouseDelta GetLastRenderDelta();

}
```

Implementation:

```cpp
RenderMouseDelta GetLastRenderDelta()
{
    RenderMouseDelta result = {};

    result.x =
        InterlockedCompareExchange(
            &g_lastRenderDeltaX, 0, 0);

    result.y =
        InterlockedCompareExchange(
            &g_lastRenderDeltaY, 0, 0);

    return result;
}
```

This is the value that a later render-camera experiment should consume.

Do not make future camera code depend directly on private Raw Input globals.

---

# Step 5: add the render-frame wrapper

Create a small module such as:

```text
src/free_rate_mouse_poll.h
src/free_rate_mouse_poll.cpp
```

The wrapper replaces the CALL at:

```text
0x004B7297
```

Conceptually:

```cpp
using CalculateCameraPositionsFn =
    int(__cdecl*)();

using PollMouseInputFn =
    void(__cdecl*)();

static constexpr uintptr_t
    kCalculateCameraPositions = 0x0043B5E0;

static constexpr uintptr_t
    kPollMouseInput = 0x005678F0;

extern "C"
int __cdecl CalculateCameraPositions_FreeRatePollHook()
{
    if (raw_input::IsEnabled() &&
        raw_input::IsBackendActive())
    {
        raw_input::BeginRenderPoll();

        auto poll =
            reinterpret_cast<PollMouseInputFn>(
                kPollMouseInput);

        poll();

        raw_input::EndRenderPoll();
    }

    auto calculateCamera =
        reinterpret_cast<CalculateCameraPositionsFn>(
            kCalculateCameraPositions);

    return calculateCamera();
}
```

Prefer RAII internally so the context flag is always restored even if the implementation changes later.

Because `0x005678F0` is already detoured by the DLL, this call enters `RawPollMouseInput()`.

The render context makes it drain the render accumulator rather than the logic accumulator.

---

# Cleaner API option

Instead of exposing `BeginRenderPoll()` / `EndRenderPoll()`, expose:

```cpp
void PollForRenderFrame();
```

Implementation:

```cpp
void PollForRenderFrame()
{
    if (!IsEnabled() || !IsBackendActive())
        return;

    RenderPollScope scope;

    auto poll =
        reinterpret_cast<PollMouseInputFn>(
            kPollMouseInput);

    poll();
}
```

Then the render wrapper becomes:

```cpp
extern "C"
int __cdecl CalculateCameraPositions_FreeRatePollHook()
{
    raw_input::PollForRenderFrame();

    auto calculateCamera =
        reinterpret_cast<CalculateCameraPositionsFn>(
            kCalculateCameraPositions);

    return calculateCamera();
}
```

This is preferred.

---

# Do not move the original logic poll

Keep the original logic call at:

```text
0x004B49CB
```

untouched.

The game should still execute its normal logic-side:

```text
PollMouseInput()
-> first mouse scaling
-> input/action processing
```

at the original cadence.

The render poll is an additional independent consumer.

This is important because the purpose of this experiment is to decouple polling frequency without changing gameplay behavior.

---

# Do not let the render poll drain gameplay input

This is the most important correctness requirement.

Wrong:

```text
WM_INPUT
   |
single accumulator
   |
render poll drains it
   |
logic gets little or zero movement
```

Correct:

```text
                 +--> logic accumulator --> logic poll
WM_INPUT packet -|
                 +--> render accumulator -> render poll
```

Every physical packet is copied into both pending streams.

---

# Inactive Raw Input behavior

Do **not** execute the legacy cursor-based `PollMouseInput()` at render FPS.

The current detour can fall back to the original legacy polling routine while Raw Input is unavailable.

That legacy routine may contain cursor recentering and behavior intended for the original logic cadence.

Therefore:

```cpp
PollForRenderFrame()
```

must return immediately unless the Raw Input backend is fully active.

The original logic-side call can retain the existing legacy fallback behavior.

---

# Focus/reset behavior

When the Raw Input state is cleared, clear both streams:

```cpp
void ClearInputState()
{
    InterlockedExchange(&g_logicAccumX, 0);
    InterlockedExchange(&g_logicAccumY, 0);

    InterlockedExchange(&g_renderAccumX, 0);
    InterlockedExchange(&g_renderAccumY, 0);

    InterlockedExchange(&g_lastRenderDeltaX, 0);
    InterlockedExchange(&g_lastRenderDeltaY, 0);

    InterlockedExchange(&g_buttonState, 0);
}
```

Do this on the same transitions already handled by the current Raw Input module:

```text
focus loss
focus recovery
suspend
destroy
re-registration/recovery
```

No stale render delta should survive a focus transition.

---

# What should be written to the game's relative X/Y

For this experiment, the render-context `PollMouseInput()` should write its render-frame delta to the same game locations:

```text
0x00F655EC = render-frame relative X
0x00F655F0 = render-frame relative Y
```

The logic-context poll will overwrite them with the logic accumulated delta when the next logic update begins.

This makes the game's standard relative-mouse memory contain a fresh render-rate sample immediately before `CalculateCameraPositions()`.

However, do not assume that `CalculateCameraPositions()` already consumes those values.

The later camera experiment must explicitly verify/use them.

---

# Optional isolation improvement

If testing shows that publishing render deltas into:

```text
0x00F655EC
0x00F655F0
```

affects unrelated render code, use a save/restore window.

Conceptually:

```cpp
LONG oldX = GameRelativeX();
LONG oldY = GameRelativeY();

raw_input::PollForRenderFrame();

LONG renderX = GameRelativeX();
LONG renderY = GameRelativeY();

raw_input::SetLastRenderDelta(renderX, renderY);

CalculateCameraPositions();

// optional
GameRelativeX() = oldX;
GameRelativeY() = oldY;
```

Only enable restoration if needed.

The preferred long-term camera consumer should read:

```cpp
raw_input::GetLastRenderDelta()
```

rather than relying on the relative globals remaining modified after the wrapper returns.

---

# Patch installation

Add a feature toggle:

```ini
[PatchGroups]
FreeRateMousePoll=1
```

Recommended dependency:

```text
FreeRateMousePoll requires RawMouseInput=1
```

If Raw Input installation fails:

```text
do not install the free-rate poll hook
```

Suggested config field:

```cpp
struct PatchConfig
{
    ...
    bool freeRateMousePoll = false;
};
```

Keep it disabled by default during development until validated.

---

# Call-site validation

Before replacing the CALL at:

```text
0x004B7297
```

verify:

```text
E8 44 43 F8 FF
```

If it does not match:

```text
log an error
do not patch
```

Do not patch unknown executable versions.

---

# Hook installation outline

Use a relative CALL rewrite.

Pseudo-code:

```cpp
bool InstallRenderPollHook()
{
    auto* callSite =
        reinterpret_cast<unsigned char*>(
            0x004B7297);

    const unsigned char expected[] =
    {
        0xE8, 0x44, 0x43, 0xF8, 0xFF
    };

    if (memcmp(callSite, expected, sizeof(expected)) != 0)
        return false;

    return WriteRelativeCall(
        callSite,
        &CalculateCameraPositions_FreeRatePollHook);
}
```

No trampoline is required for `CalculateCameraPositions()` because the wrapper can call:

```text
0x0043B5E0
```

directly.

---

# Statistics

Replace the current single poll counter with:

```cpp
volatile LONG g_logicPollCount = 0;
volatile LONG g_renderPollCount = 0;
```

Keep Raw Input report count.

Periodic diagnostic output should include:

```text
reports=...
logic_polls=...
render_polls=...
logic_dx=...
logic_dy=...
render_dx=...
render_dy=...
```

Example expected result at 240 FPS:

```text
RawInput.FreeRateStats
reports_hz=1000.4
logic_poll_hz=62.5
render_poll_hz=239.8
```

This is the main acceptance test for this experiment.

---

# Important diagnostic: movement conservation

Over a sufficiently long test interval, the total physical movement consumed by each stream should independently match the incoming Raw Input movement.

Track:

```text
raw total X/Y
logic-consumed total X/Y
render-consumed total X/Y
```

Expected:

```text
logic total ~= raw total
render total ~= raw total
```

Small differences are acceptable only at interval boundaries because pending movement may still be sitting in an accumulator.

There must not be systematic input loss.

---

# Acceptance criteria

This experiment is successful when:

1. `WM_INPUT` continues to receive the physical mouse report rate.

2. The original logic-side `PollMouseInput()` still runs at approximately 62.5 Hz.

3. An additional render-side `PollMouseInput()` runs once per rendered frame.

4. At 240 FPS, render poll statistics report approximately 240 calls per second.

5. Render polling does not reduce or fragment the movement consumed by the normal logic path.

6. Normal player sensitivity and gameplay behavior remain unchanged.

7. Focus loss/recovery does not create stale movement.

8. Disabling `FreeRateMousePoll` restores the current DLL behavior.

9. The patch does not call legacy cursor polling at render FPS when Raw Input is inactive.

10. `raw_input::GetLastRenderDelta()` returns a fresh per-render relative mouse sample.

---

# Non-goals

Do not implement any of the following in this patch:

```text
camera yaw/pitch prediction
camera interpolation
XYZ smoothing
UpdateWorldCamera changes
logic-rate changes
physics-rate changes
weapon-rate changes
network-rate changes
player orientation writes
matrix prediction
```

This patch is only about obtaining a clean `PollMouseInput()` sample at render frequency.

---

# Expected result after this patch

Before:

```text
mouse hardware       ~1000 Hz
WM_INPUT             ~1000 Hz
PollMouseInput       ~62.5 Hz
game relative X/Y    ~62.5 Hz
```

After:

```text
mouse hardware       ~1000 Hz
WM_INPUT             ~1000 Hz

logic PollMouseInput ~62.5 Hz
logic input stream   preserved

render PollMouseInput = render FPS
example:             ~240 Hz

last render delta    refreshed every render frame
```

The game now has a render-synchronized mouse polling opportunity that is independent from its normal logic input consumption.

---

# What this enables next

After this experiment is verified, the next patch can answer a much simpler question:

> Can the render camera directly consume `GetLastRenderDelta()` every frame and update its visual yaw/pitch without reproducing the entire simulation tick?

That should be tested separately.

Do not combine the two experiments initially.

First prove that free-rate polling itself is stable and conserves mouse movement.

---

# Recommended Codex implementation order

```text
1. Rename the existing g_accumX/Y concept to logic accumulators.

2. Add render accumulators.

3. Feed every accepted WM_INPUT movement packet into both streams.

4. Add thread-local render-poll context.

5. Refactor RawPollMouseInput() to drain the correct stream.

6. Add last-render-delta storage and API.

7. Add separate logic/render poll statistics.

8. Add PollForRenderFrame().

9. Add free_rate_mouse_poll module.

10. Hook 0x004B7297.

11. Poll immediately before original CalculateCameraPositions().

12. Verify:
       raw ~1000 Hz
       logic ~62.5 Hz
       render ~= FPS

13. Verify movement conservation.

14. Verify gameplay behavior is unchanged.

15. Stop here.
```

---

# Final implementation principle

The core rule for this experiment is:

> **Increase the frequency of `PollMouseInput()` without stealing input from the original 62.5 Hz logic path.**

Use two independent Raw Input accumulators, trigger the second poll from the render path, and keep all gameplay processing untouched.

This creates the free-rate mouse stream needed for the next camera experiment without yet changing camera or simulation behavior.
