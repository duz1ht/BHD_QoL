# Raw mouse regression checks

Run these in the actual game with the rebuilt 32-bit DLL. Packet unit tests do
not establish in-game firing, focus behavior or frame-time stability.

Start with `RawMouseInput=1` and **every other option explicitly set to `0`** in
`bhd_player_config.ini`; omitted options default to enabled. Repeat with `RawMouseInput=0`
as the control, then with the normal configuration.

1. Fire individual shots and hold/release automatic fire while moving the mouse,
   both over players and empty scenery. Repeat with aim/zoom held. Each physical
   press/release must reach the game once; firing must stop on release. Verify
   mouse-wheel bindings and menu clicks as well.
2. Alt+Tab while holding fire, release outside the game, then return. Repeat
   using the Windows key and minimizing/restoring. Firing must not remain held,
   and returning must not replay old movement.
3. In windowed/borderless mode with confinement disabled, click another visible
   application. It must activate normally. After Alt+Tab, verify desktop icons
   and other applications remain clickable while the game stays open.
4. At each available mouse polling rate (especially 1000/4000/8000 Hz), move
   continuously, fire and change focus repeatedly during a sustained match.
   Compare FPS/frame times, process CPU and memory with the control. Record the
   polling rate, enabled options, time to any slowdown and whether stopping mouse
   movement or changing focus recovers it. A fixed packet buffer and bounded
   drain prevent unbounded per-dispatch work; they do not prove that every
   possible cause of a long-session FPS drop is resolved.

## Implementation evidence

- `GetRawInputBuffer` supplies a 24-byte header under WOW64, unlike the 16-byte
  header returned by `GetRawInputData` in this 32-bit DLL. The packet regression
  covers both layouts, alignment padding, invalid lengths and sustained batches.
  See [Microsoft's buffered input documentation](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-getrawinputbuffer).
- Registration no longer uses `RIDEV_CAPTUREMOUSE`, `RIDEV_NOLEGACY` or
  `RIDEV_INPUTSINK`. Raw packets cannot synthesize button/wheel messages or write
  the game's cursor/button state. See [Microsoft's registration flags](https://learn.microsoft.com/en-us/windows/win32/api/winuser/ns-winuser-rawinputdevice).
- Read-only disassembly of the reference executable shows complete instructions
  at `0x005678F0`, `0x005678F3` and `0x005678F4`, ending at `0x005678FA`.
  The fallback trampoline now copies all ten bytes instead of splitting the
  six-byte `mov esi,[006112BCh]` instruction after its first two bytes.
- The native message pump at `0x00568FC0` dispatches mouse messages independently
  of the replaced mouse poll. Buttons and wheel therefore retain that path while
  the replacement poll supplies relative motion.
