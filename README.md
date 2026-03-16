# Minecraft Bedrock Auto Bot

A small Windows desktop tool (no client hack, no injection) that automates
harvesting and replanting crops on a 9×9 farm in Minecraft Bedrock Edition
using simulated keyboard and mouse input (Win32 `SendInput`).

The farm has a water block in the centre at **(5, 5)** which the bot navigates
around using BFS pathfinding.

---

## Requirements

* **Windows 10 or Windows 11** (x64)
* **Visual Studio 2022** with the *Desktop development with C++* workload
* **CMake 3.20+** (bundled with Visual Studio 2022)

No Python or third-party libraries are required. The application is a
self-contained Win32 executable.

---

## Quick start

```cmd
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
build\Release\MinecraftBedrockAutoBot.exe
```

See **[BUILD.md](BUILD.md)** for full build and usage instructions.

---

## Starting position

Stand just left of the field at position **(0, 1)**, facing **East** into the
field, with the crop item (e.g. potatoes) in the active hotbar slot.

---

## Usage

1. Build and run `MinecraftBedrockAutoBot.exe`.
2. Start Minecraft and keep its window in the foreground.
3. Set the desired timing values in the bot window:
   * **Countdown** – seconds before the bot starts (default 3 s).
   * **Abbau-Haltedauer** – how long to hold LMB to break a crop (default 0.6 s).
   * **Schritt-Dauer** – how long to hold a movement key per grid cell (default 0.25 s).
   * **Pause nach Pflanzen** – pause after right-clicking to plant (default 0.1 s).
4. Click **Start**. After the countdown the bot will:
   * Tilt the camera down toward the ground.
   * Walk every cell in a snake pattern (skipping the water block at 5, 5).
   * Hold **LMB** to break each crop, then **RMB** to replant.
   * Return to the start position.
5. Press **any key** or click **Stopp** to stop immediately.

---

## Project structure

```
src/
  main.cpp    – wWinMain entry point
  gui.h/.cpp  – Win32 window, message loop, UI logic
  bot.h/.cpp  – BFS traversal + Win32 SendInput automation
  utils.h/.cpp– String helpers, number parsing
CMakeLists.txt
BUILD.md
```
