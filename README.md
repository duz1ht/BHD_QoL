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

## Build

Cross-compile for 32-bit Windows with MinGW:

```sh
cmake -S . -B build -DCMAKE_TOOLCHAIN_FILE=mingw32-toolchain.cmake
cmake --build build
```

Alternatively, configure CMake with any i686 MinGW toolchain that produces a 32-bit Windows DLL named `dinput8.dll`.
