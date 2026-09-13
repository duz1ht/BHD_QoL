# BHD QoL

Proxy `dinput8.dll` com melhorias para **Delta Force: Black Hawk Down** (`dfbhd.exe`). A DLL carrega o DirectInput original do Windows e aplica apenas as opções ativadas no `dinput8.ini`.

## Como usar

1. Copie `dinput8.dll` para a mesma pasta de `dfbhd.exe`.
2. Copie também `dinput8.ini` para essa pasta caso queira alterar as opções.
3. Abra o jogo normalmente.

Exemplo:

```text
Pasta do jogo/
├── dfbhd.exe
├── dinput8.dll
└── dinput8.ini
```

O arquivo INI é opcional. Sem ele, a DLL usa os valores padrão indicados abaixo. Use `1` para ativar e `0` para desativar cada opção.

## Opções do `dinput8.ini`

### `[PatchGroups]`

| Opção | Padrão | O que faz |
| --- | :---: | --- |
| `BorderlessFullscreen` | `0` | Executa o jogo em modo janela sem bordas, ocupando todo o monitor. |
| `ForceDesktopResolution` | `0` | Com `BorderlessFullscreen=1`, usa a resolução do monitor como resolução interna. Em `0`, mantém a resolução escolhida no jogo e a estica para preencher a tela. Não tem efeito sem o modo sem bordas. |
| `UseCorrectAspectFOV` | `1` | Corrige o campo de visão em proporções diferentes de 4:3 sem alterar zoom, miras ou câmeras especiais. |
| `DPIAware` | `1` | Evita que a escala de DPI do Windows distorça coordenadas da janela, do monitor e do cursor. Recomendado para o modo sem bordas. |
| `RawMouseInput` | `1` | Usa o Raw Input do Windows para leitura relativa e mais confiável do mouse, preservando sensibilidade, inversão e binds do jogo. |
| `MouseScalingFix` | `1` | Corrige o arredondamento de movimentos pequenos do mouse, especialmente perceptível em miras de precisão. |
| `AdaptiveScreenCenter` | `1` | Calcula o centro da tela de acordo com a resolução atual, em vez de usar valores fixos. |
| `ClipCursorFix` | `1` | Usa a resolução atual ao limitar o cursor à janela, em vez dos limites fixos de 640×480. |
| `RestoreCursorClip` | `1` | Restaura a prisão do cursor na janela após Alt+Tab, mudança de foco, resolução ou monitor. Funciona independentemente de `RawMouseInput`. |
| `NVGResolution` | `1` | Aumenta a resolução da visão noturna de 512×256 para 2048×1024. |

### `[Logging]`

| Opção | Padrão | O que faz |
| --- | :---: | --- |
| `Enabled` | `0` | Cria, a cada execução, um arquivo `BHD_QoL_<data>_<hora>_<pid>.log` ao lado de `dfbhd.exe`. Ative para diagnosticar falhas ou recursos que não foram aplicados. |
| `RawInputStatisticsIntervalMs` | `5000` | Define, em milissegundos, o intervalo dos diagnósticos de Raw Input e confinamento do cursor no log. Aceita de `1000` a `60000` e não altera a latência do mouse. |

Configuração padrão completa:

```ini
[PatchGroups]
BorderlessFullscreen=0
ForceDesktopResolution=0
UseCorrectAspectFOV=1
DPIAware=1
RawMouseInput=1
MouseScalingFix=1
AdaptiveScreenCenter=1
ClipCursorFix=1
RestoreCursorClip=1
NVGResolution=1

[Logging]
Enabled=0
RawInputStatisticsIntervalMs=5000
```

## Compilação

É necessário gerar uma DLL Windows de 32 bits. Com MinGW:

```sh
cmake -S . -B build -DCMAKE_TOOLCHAIN_FILE=mingw32-toolchain.cmake
cmake --build build
```

O resultado deve ser uma DLL chamada `dinput8.dll`.

## Compatibilidade

Antes de aplicar cada alteração, a DLL confere os bytes esperados do executável. Em uma versão incompatível de `dfbhd.exe`, a alteração afetada não é aplicada. Ative `Logging.Enabled=1` para conferir o resultado.

## Para agentes LLM

`Knowledge_Base/` é uma área de referência somente para leitura. Nenhum arquivo ou diretório dentro dela deve ser alterado.
