# Build Instructions

## Requirements

| Requirement | Minimum version |
|---|---|
| Windows | 10 or 11 (x64) |
| Visual Studio | 2022 (any edition) |
| MSVC toolset | v143 |
| CMake | 3.20 (included with VS 2022) |

## Build

### Option A – Visual Studio IDE

1. File > Open > CMake, select CMakeLists.txt.
2. Select x64-Release in the toolbar.
3. Build > Build All (Ctrl+Shift+B).

### Option B – Developer Command Prompt

    cmake -S . -B build -G "Visual Studio 17 2022" -A x64
    cmake --build build --config Release
    build\Release\MinecraftBedrockAutoBot.exe

## Project structure

    src/
      main.cpp              wWinMain entry point
      gui.h / gui.cpp       Win32 window, controls, log list-box
      bot.h / bot.cpp       State machine + SendInput automation
      state_machine.h       BotState enum
      screen_capture.h/.cpp IScreenCapture interface + GDI impl
      ocr.h                 IOcr interface + NullOcr stub
      parser.h / parser.cpp F3 overlay coordinate parser
      utils.h / utils.cpp   String helpers, number parsing
    CMakeLists.txt
    BUILD.md
