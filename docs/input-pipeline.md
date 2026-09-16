# Mouse input pipeline and late-latching status

## Authoritative path

The supported executable's `PollMouseInput` routine is at `0x005678F0`. It writes
relative X/Y to `0x00F655EC` and `0x00F655F0`. Those values are later multiplied
by the game's Q16 sensitivity values; `MouseScalingFix` changes only the rounding
of that multiplication and deliberately owns its own fractional remainder. Game
code then applies normal sensitivity, ADS/scope scaling and vertical inversion
before updating the actor's authoritative full-turn yaw and bounded pitch. Those
authoritative fields feed gameplay, firing, hit detection, raycasts and network
state at the simulation cadence (approximately 62.5 Hz).

The exact producer boundary between that actor update and the camera root consumed
by the renderer has **not** yet been proven. In particular, the viewmodel call at
`0x00488AF6` builds a 4x4 stack-local matrix from `0x0096C440`, but static analysis
alone does not prove which rendered frame first contains a particular poll.
Applying raw counts directly as angles, or removing them one frame too early,
would cause double application or a visible jump.

## Captured and consumed accounting

Raw Input therefore keeps independent monotonic captured and authoritatively
consumed totals, report/poll sequences, latest-report timestamp, and a focus
activation generation. Every existing input reset rebases both sides. The renderer
can only read `GetPendingVisualMouse()`; it cannot drain `g_accumX/g_accumY` or
change the authoritative scaler's remainder.

## Diagnostic, fail-closed rollout

`VisualMouseLateLatching=1` currently enables diagnostics only. Once per main
visual frame it snapshots pending input and logs `VisualInterpolation.Stats`, but
returns an invalid visual angle and leaves both camera and viewmodel stack copies
untouched. This is intentional: runtime traces must establish the exact poll to
camera-root producer step before correction can safely be enabled.

Future correction may make camera/viewmodel response follow render rate, while
simulation, aim direction, hit detection and firing remain at about 62.5 Hz. This
necessarily permits a short-lived difference between displayed aim and the
authoritative direction. It reduces perceived visual latency; it does not raise
the simulation tick rate. Unknown scale, focus, camera mode, sequence generation,
or callsite must always fall back to the native path.

## Runtime validation plan

With a stationary camera and controlled mouse motion, compare the option at 0/1
and record capture-to-render plus capture-to-authoritative-update sequences. Test
hip-fire, ADS, scopes, recoil, vehicles, mounted weapons, menus, pause and focus
loss. Before enabling correction, verify identical real aim position, shots,
hitboxes and network state in both modes and prove that consumption and the new
authoritative root become visible in the same producer step.
