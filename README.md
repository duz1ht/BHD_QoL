# BHD QoL

A `dinput8.dll` proxy with quality-of-life improvements for **Delta Force: Black Hawk Down** (`dfbhd.exe`). The DLL loads the original Windows DirectInput library and applies only the options enabled in `dinput8.ini`.

## Installation

1. Copy `dinput8.dll` to the same directory as `dfbhd.exe`.
2. Copy `dinput8.ini` to that directory if you want to change the settings.
3. Launch the game normally.

Example:

```text
Game directory/
├── dfbhd.exe
├── dinput8.dll
└── dinput8.ini
```

The INI file is optional. If it is missing, the DLL uses the default values shown below. Use `1` to enable an option and `0` to disable it.

## `dinput8.ini` options

### `[PatchGroups]`

| Option | Default | Description |
| --- | :---: | --- |
| `BorderlessFullscreen` | `1` | Runs the game in a borderless window that covers the entire monitor. |
| `ForceDesktopResolution` | `1` | With `BorderlessFullscreen=1`, uses the monitor resolution as the internal render resolution. At `0`, the resolution selected in the game is stretched to fill the screen. Has no effect when borderless mode is disabled. |
| `UseCorrectAspectFOV` | `1` | Corrects the field of view for internal render resolutions wider than 4:3 without changing zoom, scopes, or special cameras. |
| `DPIAware` | `1` | Prevents Windows DPI scaling from distorting window, monitor, and cursor coordinates. Recommended for borderless mode. |
| `RawMouseInput` | `1` | Uses Windows Raw Input for more reliable relative mouse input while preserving the game's sensitivity, inversion, and bindings. |
| `FreeRateMousePoll` | `1` | Samples a separate Raw Input stream once per rendered camera frame without consuming movement from the authoritative logic stream. Requires `RawMouseInput`. |
| `HighRateCameraRotation` | `1` | Maintains render-only first-person yaw and pitch from newly arrived Raw Input without reproducing the legacy persistent yaw filter. Requires `FreeRateMousePoll`. |
| `MouseScalingFix` | `1` | Fixes rounding of small mouse movements, especially noticeable when using scoped weapons. |
| `AdaptiveScreenCenter` | `1` | Calculates the screen center from the current resolution instead of using fixed values. |
| `ScaleCursorClipToResolution` | `1` | Uses the current resolution when confining the cursor instead of fixed 640x480 bounds. |
| `RestoreCursorClip` | `1` | Restores cursor confinement after Alt+Tab, focus, resolution, display, or window changes. Works independently of `RawMouseInput`. |

`HighRateCameraRotation` is deliberately limited to the normal local-player,
on-foot first-person camera. It keeps its yaw and pitch exclusively in the
presentation layer, consumes each accepted Raw Input count once, and rebases
after focus, ownership, camera-mode, pause, or mount transitions. Unsupported
camera states retain the native result. The authoritative player orientation
and the original 62.5 Hz yaw filter are not patched.

## Overlay de diagnóstico

O build gera somente um executável auxiliar, `BHD_QoL_Overlay.exe`. Ele não
injeta código e não lê a memória do jogo diretamente: a DLL publica telemetria
em memória compartilhada e o executável apenas a apresenta em uma janela
transparente, click-through e posicionada sobre a área do jogo.

Inicie o jogo com `dinput8.dll` e depois execute `BHD_QoL_Overlay.exe`. O painel
mostra, em Hz:

- pacotes aceitos de Raw Input;
- chamadas autoritativas de `PollMouseInput`;
- chamadas do call site de câmera que dispara o polling de render (não é
  rotulado como `Present`/FPS real);
- chamadas do avaliador e frames em que a câmera visual foi realmente aplicada;
- frames em que o yaw visual mudou;
- atualizações do yaw oficial do jogador;
- contagens Raw Input pendentes e recebidas no frame mais recente.
- motivo exato de qualquer fallback, junto dos valores de modo, pausa, mouse,
  objetos de input, jogador local, proprietário da câmera e ride target.

Durante movimento contínuo, se as execuções/mudanças visuais acompanharem o
FPS de render e ultrapassarem claramente as mudanças oficiais, o painel mostra
`CAMERA VISUAL LIVRE PELO FPS DO RENDER`. Caso contrário, sinaliza que a câmera
ainda aparenta estar limitada pela lógica. `F8` oculta/exibe o painel e `F9`
encerra o overlay.

Se a conexão não ocorrer, o painel agora mostra o erro de `OpenFileMapping`.
Erro `2` indica que a DLL não publicou a memória compartilhada (normalmente DLL
antiga/incorreta ou proxy não carregado); erro `5` indica diferença de permissão.
A DLL e o overlay devem sempre ser copiados do mesmo build. O nome da memória
compartilhada é estável e a DLL permite leitura mesmo quando jogo e overlay são
iniciados com níveis de elevação diferentes.

### `[Logging]`

| Option | Default | Description |
| --- | :---: | --- |
| `Enabled` | `0` | Creates a new `BHD_QoL_<date>_<time>_<pid>.log` beside `dfbhd.exe` for each session. Enable it to diagnose failures or features that were not applied. |
| `RawInputStatisticsIntervalMs` | `5000` | Sets the interval, in milliseconds, for Raw Input and cursor-confinement diagnostics in the log. Accepts values from `1000` to `60000` and does not affect mouse latency. |

Complete default configuration:

```ini
[PatchGroups]
BorderlessFullscreen=1
ForceDesktopResolution=1
UseCorrectAspectFOV=1
DPIAware=1
RawMouseInput=1
FreeRateMousePoll=1
HighRateCameraRotation=1
MouseScalingFix=1
AdaptiveScreenCenter=1
ScaleCursorClipToResolution=1
RestoreCursorClip=1

[Logging]
Enabled=0
RawInputStatisticsIntervalMs=5000
```

## Building

The project must be built as a 32-bit Windows DLL. With MinGW:

```sh
cmake -S . -B build -DCMAKE_TOOLCHAIN_FILE=mingw32-toolchain.cmake
cmake --build build
```

Os artefatos utilizáveis são `dinput8.dll` e o único executável auxiliar,
`BHD_QoL_Overlay.exe`. Os antigos executáveis individuais de teste não são mais
gerados pelo CMake.

## Compatibility

Before applying each modification, the DLL verifies the expected bytes in the executable. If the `dfbhd.exe` version is incompatible, the affected modification is not applied. Set `Logging.Enabled=1` to check the results.
