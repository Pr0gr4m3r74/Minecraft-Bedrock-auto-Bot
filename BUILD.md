# Build Instructions – Minecraft Bedrock Auto Bot (C++17 / Win32)

## Requirements

| Requirement | Minimum version |
|---|---|
| Windows | 10 or 11 (x64) |
| Visual Studio | 2022 (any edition, including Community) |
| MSVC toolset | v143 (installed with VS 2022) |
| CMake | 3.20 – included with Visual Studio 2022 |

> **Note:** The project targets **MSVC only**.  
> It will not compile with GCC, Clang, or MinGW.

---

## Option A – Build with the Visual Studio IDE

1. Open **Visual Studio 2022**.
2. Choose **File → Open → CMake…** and select the `CMakeLists.txt` in the
   project root.
3. Visual Studio will automatically run CMake configuration.
4. In the **Configuration** drop-down (toolbar), select **x64-Release** (or
   **x64-Debug** for a debug build).
5. Press **Ctrl+Shift+B** (or **Build → Build All**) to compile.
6. The output executable is located at:
   ```
   out\build\x64-Release\MinecraftBedrockAutoBot.exe
   ```
   (the exact path depends on the CMake preset Visual Studio creates).

---

## Option B – Build from the Developer Command Prompt

1. Open a **Developer Command Prompt for VS 2022** (Start menu → Visual
   Studio 2022 → Developer Command Prompt).
2. Navigate to the project root:
   ```cmd
   cd path\to\Minecraft-Bedrock-auto-Bot
   ```
3. Configure and build:
   ```cmd
   cmake -S . -B build -G "Visual Studio 17 2022" -A x64
   cmake --build build --config Release
   ```
4. The compiled executable is at:
   ```
   build\Release\MinecraftBedrockAutoBot.exe
   ```

---

## Running the bot

1. Launch **Minecraft Bedrock Edition**.
2. Double-click `MinecraftBedrockAutoBot.exe` (no installation required).
3. Follow the on-screen instructions in the welcome dialog.
4. Position your player at **(0, 1)**, facing **East**, holding **potatoes**
   (or the crop you wish to replant).
5. Click **Start**. After the countdown the bot will:
   - Look down at the field.
   - Walk every cell in a snake pattern (skipping the water block at (5, 5)).
   - Hold **LMB** to break each mature crop, then **RMB** to replant.
   - Return to the start position when done.
6. Press **any key** or click **Stopp** to interrupt the bot at any time.

---

## Uninstalling

Simply delete `MinecraftBedrockAutoBot.exe`. No registry entries or additional
files are created by the application.

---

## Project structure

```
Minecraft-Bedrock-auto-Bot/
├── src/
│   ├── main.cpp      – wWinMain entry point
│   ├── gui.h         – AppState, custom messages, control IDs
│   ├── gui.cpp       – Win32 window class, message loop, UI logic
│   ├── bot.h         – Bot class and BotConfig declarations
│   ├── bot.cpp       – BFS traversal, SendInput automation
│   ├── utils.h       – Utility declarations
│   └── utils.cpp     – String conversion, number parsing
├── CMakeLists.txt    – CMake build configuration (MSVC / x64)
└── BUILD.md          – This file
```
