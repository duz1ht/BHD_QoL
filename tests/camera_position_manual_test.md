# Camera position regression checks

Use the rebuilt x86 winmm.dll and compare HighFrequencyCameraPosition=0/1 at
the same FPS. Keep the other options equal between runs.

1. Look at a fixed pole/wall edge and strafe left/right, then walk diagonally,
   forwards/backwards and over slopes. Confirm the scenery moves more smoothly.
2. Stand still and move the mouse: yaw/pitch must keep the current Raw Input
   response. Combine mouse look with walking, zoom and scopes.
3. Check weapon/hands against the scenery while moving. Repeat with
   HighFrequencyCamera=0, RawMouseInput=0, HighFrequencyViewmodel=1 and position
   smoothing enabled to verify translation and weapon synchronization alone.
4. Pause/resume, Alt+Tab, open menus, die/respawn, teleport/change map, and
   enter/leave vehicles. Unsupported cameras and mounts must use native
   translation; resuming must not blend from an old position.
5. Repeat with position smoothing disabled and with all three camera/viewmodel
   options disabled. Check that native camera and firing behavior remain usable.
6. Run a sustained match at high FPS/polling rates and compare frame-time
   stability. Position smoothing uses one fixed history and no per-frame heap
   allocations; the automated tests do not establish in-game frame pacing.

Automated coverage includes XYZ/phase math, discontinuity guards, counter wrap,
overflow rejection and native execution of the actual capture-stub generator,
including its player filter, register/flag preservation, x87/SSE preservation
and replay of the replaced native instruction.
The backend tests execute the real callbacks against a synthetic module image,
including the scheduler's post-producer fast-counter increment, slow-counter
timing, camera XYZ-only writes, focus/pause/mount guards, signature preflight
and rollback after an injected code-write failure.
