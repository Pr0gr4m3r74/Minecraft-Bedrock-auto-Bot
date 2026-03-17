// bot.h – Farm-bot logic declarations.
//
// The bot automates harvesting and replanting crops on a configurable
// rectangular farm using Win32 SendInput to simulate keyboard/mouse input.
// A finite-state machine (see state_machine.h) drives each phase of the run
// and ensures safe recovery when OCR validation or timing checks fail.
//
// Screen capture (IScreenCapture) and OCR (IOcr) are injected as interfaces
// so OpenCV / Tesseract backends can be added later without modifying this file.
//
// Windows-only; compiled with MSVC targeting Windows 10/11 x64.

#pragma once

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>

#include "state_machine.h"
#include "screen_capture.h"
#include "ocr.h"
#include "parser.h"

#include <atomic>
#include <functional>
#include <memory>
#include <utility>
#include <vector>

// ---------------------------------------------------------------------------
// Farm configuration
// ---------------------------------------------------------------------------

/// All parameters that control a single bot run.
struct FarmConfig {
    int    farmWidth      = 9;      ///< Farm width  (columns, X-axis).
    int    farmHeight     = 9;      ///< Farm height (rows,    Z-axis).
    double blockDelay     = 0.25;   ///< Seconds to hold a movement key per grid cell.
    double breakDelay     = 0.60;   ///< Seconds to hold LMB while breaking a crop.
    double replantPause   = 0.10;   ///< Pause (seconds) after right-clicking to plant.
    double waitForGrowth  = 300.0;  ///< Seconds to wait for crops to grow (0 = skip).
    int    resetInterval  = 9;      ///< Rows between safe-point returns (0 = never).
    double countdownSecs  = 3.0;    ///< Start countdown (managed by the GUI timer).
    UINT   startStopVk    = VK_F6;  ///< Virtual-key code for the start/stop hotkey.
};

// ---------------------------------------------------------------------------
// Bot class
// ---------------------------------------------------------------------------

/// Automates harvesting and replanting on a rectangular Minecraft farm.
class Bot {
public:
    // ---- Public types ----
    using Pos            = std::pair<int, int>;
    using StatusCallback = std::function<void(const wchar_t*)>;
    using LogCallback    = std::function<void(const wchar_t*)>;

    // ---- Constants ----
    static constexpr int    START_X           = 0;    ///< Player start X (left of farm).
    static constexpr int    START_Y           = 1;    ///< Player start Y.
    static constexpr double LOOK_DOWN_RATIO   = 0.25; ///< Fraction of screen height for camera tilt.
    static constexpr float  OCR_MIN_CONFIDENCE= 0.5f; ///< OCR below this triggers ErrorRecovery.
    static constexpr double MAX_POS_TOLERANCE = 3.0;  ///< Max allowed XZ deviation (blocks).

    // ---- Constructor ----
    Bot(std::atomic<bool>&              stopFlag,
        std::atomic<bool>&              ignoreStopKeys,
        StatusCallback                  statusCb,
        LogCallback                     logCb,
        std::shared_ptr<IScreenCapture> screenCapture,
        std::shared_ptr<IOcr>           ocr);

    /// Execute the full farm run. Designed to run on a dedicated worker thread.
    void Run(const FarmConfig& cfg);

    /// Return the current state (thread-safe).
    BotState CurrentState() const noexcept {
        return m_state.load(std::memory_order_acquire);
    }

private:
    // ---- State machine ----
    std::atomic<BotState> m_state{BotState::Idle};
    void TransitionTo(BotState next, const wchar_t* reason = nullptr);

    // ---- Per-run context ----
    PlayerPos m_origin;
    bool      m_originSet = false;

    // ---- Coordinate / OCR helpers ----
    void      ToggleF3Open();
    void      ToggleF3Close();
    ScreenBitmap CaptureF3Region() const;
    PlayerPos ReadCoordinates();
    bool      ValidatePosition(const PlayerPos& current, const PlayerPos& expected);

    // ---- Focus helpers ----
    bool MinecraftHasFocus() const;
    bool WaitForFocus();

    // ---- State handlers ----
    void HandleResetPosition(const FarmConfig& cfg);
    void HandleMoveToRowStart(const FarmConfig& cfg, int row, Pos& pos);
    void HandleFarmRow(const FarmConfig& cfg, int row, Pos& pos);
    void HandleReturnToOrigin(const FarmConfig& cfg, Pos& pos);
    void HandleWaitForGrowth(const FarmConfig& cfg);
    void HandleErrorRecovery(const std::wstring& reason);

    // ---- Movement helpers ----
    void LookDown();
    void HarvestAndPlant(double breakDelay, double replantPause);
    void StepTowards(Pos current, Pos next, double blockDelay);

    // ---- BFS path-planning ----
    std::vector<Pos> TraversalOrder(int width, int height)                       const;
    std::vector<Pos> PathBetween(Pos start, Pos goal, int width, int height)     const;
    std::vector<Pos> Neighbors(Pos node, int width, int height)                  const;

    // ---- Low-level Win32 input ----
    static void PressKey(WORD vk);
    static void ReleaseKey(WORD vk);
    static void MouseDown(DWORD flags);
    static void MouseUp(DWORD flags);
    static void MouseClick(DWORD downFlags, DWORD upFlags);
    static bool WaitOrStop(std::atomic<bool>& stop, double seconds);

    // ---- Members ----
    std::atomic<bool>&              m_stop;
    std::atomic<bool>&              m_ignoreStop;
    StatusCallback                  m_statusCb;
    LogCallback                     m_logCb;
    std::shared_ptr<IScreenCapture> m_capture;
    std::shared_ptr<IOcr>           m_ocr;

    // ---- Inline helpers ----
    void Log(const wchar_t* msg);
    void Status(const wchar_t* msg);
};
