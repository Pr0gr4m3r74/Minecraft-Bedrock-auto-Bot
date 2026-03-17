# Minecraft Bedrock Auto Bot

A Windows desktop tool (no client hack, no injection) that automates
harvesting and replanting crops on a configurable rectangular farm in
Minecraft Bedrock Edition using simulated keyboard and mouse input
(Win32 `SendInput`).

A finite-state machine drives each farming phase, and an exchangeable
OCR backend allows coordinate-based position validation when a real OCR
engine (e.g. Tesseract) is plugged in.

---

## Requirements

* **Windows 10 or Windows 11** (x64)
* **Visual Studio 2022** with the *Desktop development with C++* workload
* **CMake 3.20+** (bundled with Visual Studio 2022)

No Python or third-party libraries are required.  The application is a
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

## GUI controls

| Field | Description | Default |
|---|---|---|
| Farm-Breite | Farm width in blocks (columns) | 9 |
| Farm-Hoehe | Farm height in blocks (rows) | 9 |
| Block-Delay | Seconds to hold movement key per block | 0.25 |
| Abbau-Haltedauer | Seconds to hold LMB while breaking a crop | 0.6 |
| Pause nach Pflanzen | Pause after right-clicking to plant | 0.1 |
| Wartezeit Wachstum | Seconds to wait for crops to grow (0 = skip) | 300 |
| Reset-Intervall | Rows between safe-point returns (0 = off) | 9 |
| Start/Stop-Hotkey | Global hotkey to toggle the bot (e.g. F6) | F6 |
| Countdown | Seconds before the bot starts | 3 |

---

## State machine

The bot transitions through the following states:

| State | Description |
|---|---|
| **Leerlauf** (Idle) | Bot not running |
| **Position pruefen** (ResetPosition) | Verify / navigate back to saved origin |
| **Zur Reihe** (MoveToRowStart) | Travel to the first cell of the next row |
| **Reihe ernten** (FarmRow) | Harvest and replant every cell in the row |
| **Zum Ursprung** (ReturnToOrigin) | Return to origin after reset interval or full cycle |
| **Warten auf Wachstum** (WaitForGrowth) | Timer wait for crops to mature |
| **Fehlerkorrektur** (ErrorRecovery) | Handle unexpected state; stop safely |

---

## Coordinate validation (optional)

The bot uses two pluggable interfaces:

* **IScreenCapture** – captures the F3 debug overlay region.
  Default:  (Win32 GDI BitBlt).
* **IOcr** – extracts text from the captured bitmap.
  Default:  (stub – coordinate validation disabled).

Replace  in  /  with a
 implementation to enable live coordinate validation.
When OCR confidence falls below 50 % the bot enters 
and stops safely.

---

## Project structure

```
src/
  main.cpp            – wWinMain entry point
  gui.h / gui.cpp     – Win32 window, message loop, UI controls, log window
  bot.h / bot.cpp     – State machine, BFS traversal, SendInput automation
  state_machine.h     – BotState enum and state-name helper
  screen_capture.h/.cpp – IScreenCapture interface + GDI implementation
  ocr.h               – IOcr interface + NullOcr stub
  parser.h / parser.cpp – CoordParser: parse F3 overlay text for XYZ coords
  utils.h / utils.cpp – String helpers, number parsing
CMakeLists.txt
BUILD.md
```
