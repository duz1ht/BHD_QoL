# BHD QoL dinput8 proxy

This repository contains a `dinput8.dll` proxy for *Delta Force: Black Hawk Down* (`dfbhd.exe`). Put the compiled DLL in the same directory as `dfbhd.exe`; Windows will load it before the system DirectInput 8 DLL, then the proxy forwards DirectInput calls to the real system DLL.

## Patch included

The DLL patches `SetupNVGViewPort()` at process attach time to increase the night-vision viewport from 512x256 to 2048x1024:

| Address | Original | Patched |
| --- | --- | --- |
| `0052A934` | `MOV EAX,0x200` | `MOV EAX,0x800` |
| `0052A94C` | `MOV [00E0498C],0x200` | `MOV [00E0498C],0x800` |
| `0052A956` | `MOV [00E04990],0x100` | `MOV [00E04990],0x400` |

The DLL also installs dynamic-resolution cursor and viewport hooks:

| Address | Original | Patched | Purpose |
| --- | --- | --- | --- |
| `004BC711` | `MOV DWORD PTR [009FB3D8],EBX` | `JMP 005E4879` | Compute additional resolution values in `LoadingGame()` |
| `00567972` | `ADD EAX,0xF0` | `JMP 005E48C0` | Include dynamic resolutions in `PollMouseInput()` cursor positioning |
| `004623D2` | `MOV DWORD PTR [EBP + -0x8],0x27F` | `JMP 005E4912` | Include dynamic resolutions in `InitializeInGameSystems()` ClipCursor setup |
| `004628D3` | `MOV DWORD PTR [EBP + -0x8],0x27F` | `JMP 005E492B` | Include dynamic resolutions in `ClipCursorToViewPort()` |

Each patch checks the expected bytes before writing, so unsupported executables are left unchanged.

The DLL also hooks the game's `ClipCursor` import and remembers its most recent valid clipping
rectangle. After the game regains focus (including after a fullscreen display-mode change), a short
deferred window-message callback restores that rectangle when the game is foreground, focused,
visible, and not minimized. This prevents the game from remaining in its inactive-performance state
after Alt-Tab.


## Runtime configuration

Copy `dinput8.ini` next to `dfbhd.exe` and the compiled `dinput8.dll` to enable or disable patch groups at runtime:

```ini
[PatchGroups]
NVGResolution=1
DynamicResolution=1
MouseCursorFix=1
ClipCursorFix=1
CursorClipRecovery=1

[Debug]
LogAppliedPatches=0
```

Set a patch group to `1` to enable it or `0` to disable it. `CursorClipRecovery` controls the
Alt-Tab recovery hook independently of the dynamic-resolution `ClipCursorFix`. If `dinput8.ini` is
missing, the DLL uses the same defaults shown above. `LogAppliedPatches=1` writes patch status
messages to the debugger through `OutputDebugStringA`.

## Knowledge_Base policy

The `Knowledge_Base/` directory is a read-only reference area for LLM agents such as Codex. It may contain code, examples, or material extracted from other projects so that maintainers and agents can study that knowledge and use it as a basis for implementing changes in the main project tree.

Do not create, edit, move, rename, delete, format, or otherwise modify files under `Knowledge_Base/`. All implementation changes must be applied outside `Knowledge_Base/`, in the main repository files.

## Build

Cross-compile for 32-bit Windows with MinGW:

```sh
cmake -S . -B build -DCMAKE_TOOLCHAIN_FILE=mingw32-toolchain.cmake
cmake --build build
```

Alternatively, configure CMake with any i686 MinGW toolchain that produces a 32-bit Windows DLL named `dinput8.dll`.
