// bot.h – Minecraft Bedrock farm-bot logic declarations.
//
// The bot automates harvesting and replanting crops on a 9x9 grid.
// It uses Win32 SendInput to simulate keyboard and mouse actions and
// BFS to navigate between farm cells in a snake-pattern traversal order.
//
// Windows-only; compiled with MSVC targeting Windows 10/11 x64.

#pragma once

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>

#include <atomic>
#include <functional>
#include <utility>
#include <vector>

// ---------------------------------------------------------------------------
// Bot configuration
// ---------------------------------------------------------------------------

/// Parameters that control timing for a single bot run.
struct BotConfig {
    double breakTime    = 0.6;   ///< Seconds to hold LMB while breaking a crop.
    double stepTime     = 0.25;  ///< Seconds to hold a movement key per grid cell.
    double replantPause = 0.1;   ///< Pause after right-clicking to plant a seed.
};

// ---------------------------------------------------------------------------
// Bot class
// ---------------------------------------------------------------------------

/// Automates harvesting and replanting on a 9x9 Minecraft Bedrock farm.
///
/// The player must be standing at START_POS facing East (looking at the field)
/// with seeds/crops in the active hotbar slot before Run() is called.
/// Run() blocks until the full traversal completes or the stop flag is set.
class Bot {
public:
    // Farm layout constants
    static constexpr int    GRID_SIZE       = 9;    ///< Side length of the square farm grid.
    static constexpr int    START_X         = 0;    ///< X of the player start position (just left of the field).
    static constexpr int    START_Y         = 1;    ///< Y of the player start position.
    static constexpr int    WATER_X         = 5;    ///< X of the water block in the centre (impassable).
    static constexpr int    WATER_Y         = 5;    ///< Y of the water block in the centre.
    static constexpr double LOOK_DOWN_RATIO = 0.25; ///< Fraction of screen height used for the initial camera tilt.

    using Pos            = std::pair<int, int>;
    /// Called on the bot thread whenever the displayed status should change.
    using StatusCallback = std::function<void(const wchar_t*)>;

    /// @param stopFlag       Set to true from any thread to interrupt the run.
    /// @param ignoreStopKeys Set to true while a movement key is held so the
    ///                       keyboard hook does not misinterpret the injected
    ///                       key-down event as a user stop request.
    /// @param statusCb       Invoked (on the bot thread) to report progress.
    Bot(std::atomic<bool>& stopFlag,
        std::atomic<bool>& ignoreStopKeys,
        StatusCallback     statusCb);

    /// Execute the full farm run. Designed to run on a dedicated worker thread.
    void Run(const BotConfig& cfg);

private:
    std::atomic<bool>& m_stop;
    std::atomic<bool>& m_ignoreStop;
    StatusCallback     m_status;

    // --- Camera / movement / action helpers ---
    void LookDown();
    void HarvestAndPlant(double breakTime, double replantPause);
    void StepTowards(Pos current, Pos next, double stepTime);

    // --- Path planning ---
    std::vector<Pos> TraversalOrder()                    const;
    std::vector<Pos> PathBetween(Pos start, Pos goal)    const;
    std::vector<Pos> Neighbors(Pos node)                 const;

    // --- Low-level Win32 input wrappers ---
    static void PressKey(WORD vk);
    static void ReleaseKey(WORD vk);
    static void MouseDown(DWORD flags);
    static void MouseUp(DWORD flags);
    static void MouseClick(DWORD downFlags, DWORD upFlags);

    /// Sleep for `seconds`, waking early if `stop` becomes true.
    /// @return true if the stop flag caused an early wake-up.
    static bool WaitOrStop(std::atomic<bool>& stop, double seconds);
};
