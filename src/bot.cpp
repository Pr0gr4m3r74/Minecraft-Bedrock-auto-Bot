// bot.cpp – Minecraft Bedrock farm-bot logic implementation.
//
// Coordinate system (mirrors the original Python implementation):
//   x increases toward the East  (forward for the player).
//   y increases toward the North (strafe-left for the player).
//   Player starts at (START_X=0, START_Y=1), facing East.
//   Water block sits at (WATER_X=5, WATER_Y=5) and is impassable.

#include "bot.h"

#include <algorithm>
#include <chrono>
#include <deque>
#include <map>
#include <unordered_map>

// ---------------------------------------------------------------------------
// Internal helpers
// ---------------------------------------------------------------------------

/// FNV-inspired hash for std::pair<int,int> for use in unordered_map.
struct PairHash {
    std::size_t operator()(const std::pair<int, int>& p) const noexcept {
        std::size_t h1 = std::hash<int>{}(p.first);
        std::size_t h2 = std::hash<int>{}(p.second);
        // Combine using a golden-ratio constant to reduce collisions.
        return h1 ^ (h2 * 0x9e3779b9u + 0x6b37'4651u + (h1 << 6) + (h1 >> 2));
    }
};

/// Maps a (dx, dy) direction vector to the corresponding virtual-key code.
/// The player faces East (+x); y increases North (strafe-left = A key).
static const std::map<std::pair<int, int>, WORD> kDirToVK{
    {{ 1,  0}, 'W'},  ///< East  → move forward
    {{-1,  0}, 'S'},  ///< West  → move backward
    {{ 0,  1}, 'A'},  ///< North → strafe left
    {{ 0, -1}, 'D'},  ///< South → strafe right
};

// ---------------------------------------------------------------------------
// Bot constructor
// ---------------------------------------------------------------------------

Bot::Bot(std::atomic<bool>& stopFlag,
         std::atomic<bool>& ignoreStopKeys,
         StatusCallback     statusCb)
    : m_stop{stopFlag}
    , m_ignoreStop{ignoreStopKeys}
    , m_status{std::move(statusCb)}
{}

// ---------------------------------------------------------------------------
// Run – main entry point called on the worker thread
// ---------------------------------------------------------------------------

void Bot::Run(const BotConfig& cfg) {
    // Tilt the camera down so the player looks at the crop blocks.
    LookDown();

    // Pre-compute BFS paths for the full traversal in one pass.
    auto order = TraversalOrder();
    std::vector<std::vector<Pos>> paths;
    paths.reserve(order.size());

    Pos pos = {START_X, START_Y};
    for (const auto& target : order) {
        paths.push_back(PathBetween(pos, target));
        pos = target;
    }

    // Execute the traversal.
    pos = {START_X, START_Y};
    for (std::size_t i = 0; i < order.size(); ++i) {
        if (m_stop.load(std::memory_order_acquire)) break;

        // Walk the BFS path to the next target cell.
        for (std::size_t j = 1; j < paths[i].size(); ++j) {
            if (m_stop.load(std::memory_order_acquire)) break;
            StepTowards(pos, paths[i][j], cfg.stepTime);
            pos = paths[i][j];
        }

        if (m_stop.load(std::memory_order_acquire)) break;

        // Harvest and replant on every cell except the start position.
        if (pos != Pos{START_X, START_Y}) {
            HarvestAndPlant(cfg.breakTime, cfg.replantPause);
        }
    }

    if (!m_stop.load(std::memory_order_acquire)) {
        m_status(L"Fertig - zurück an Ausgangsposition");
    }
}

// ---------------------------------------------------------------------------
// Camera helper
// ---------------------------------------------------------------------------

void Bot::LookDown() {
    int w = GetSystemMetrics(SM_CXSCREEN);
    int h = GetSystemMetrics(SM_CYSCREEN);

    // Move the cursor to the centre of the primary screen (absolute coords).
    // Absolute coordinates must be normalised to the 0-65535 range.
    INPUT in{};
    in.type     = INPUT_MOUSE;
    in.mi.dx    = static_cast<LONG>((w / 2) * 65535L / w);
    in.mi.dy    = static_cast<LONG>((h / 2) * 65535L / h);
    in.mi.dwFlags = MOUSEEVENTF_MOVE | MOUSEEVENTF_ABSOLUTE;
    SendInput(1, &in, sizeof(INPUT));

    // Give the game a moment to register the position.
    Sleep(150);

    // Tilt the camera down by moving the mouse downward (relative movement).
    in.mi.dx    = 0;
    in.mi.dy    = static_cast<LONG>(h * LOOK_DOWN_RATIO);
    in.mi.dwFlags = MOUSEEVENTF_MOVE;
    SendInput(1, &in, sizeof(INPUT));

    // Allow the camera animation to settle before starting movement.
    Sleep(200);
}

// ---------------------------------------------------------------------------
// Harvest / plant helper
// ---------------------------------------------------------------------------

void Bot::HarvestAndPlant(double breakTime, double replantPause) {
    // Hold the left mouse button to break the mature crop.
    MouseDown(MOUSEEVENTF_LEFTDOWN);
    if (WaitOrStop(m_stop, breakTime)) {
        MouseUp(MOUSEEVENTF_LEFTUP);
        return;
    }
    MouseUp(MOUSEEVENTF_LEFTUP);

    // Right-click to plant a new seed in the freshly tilled soil.
    MouseClick(MOUSEEVENTF_RIGHTDOWN, MOUSEEVENTF_RIGHTUP);
    if (replantPause > 0.0) {
        WaitOrStop(m_stop, replantPause);
    }
}

// ---------------------------------------------------------------------------
// Movement helper
// ---------------------------------------------------------------------------

void Bot::StepTowards(Pos current, Pos next, double stepTime) {
    int dx = next.first  - current.first;
    int dy = next.second - current.second;

    auto it = kDirToVK.find({dx, dy});
    if (it == kDirToVK.end()) return;

    // Set the ignore flag *before* injecting the key-down event so the
    // low-level keyboard hook does not misinterpret it as a stop request.
    m_ignoreStop.store(true, std::memory_order_seq_cst);
    PressKey(it->second);
    WaitOrStop(m_stop, stepTime);
    ReleaseKey(it->second);
    // Clear the ignore flag *after* the key-up event has been injected.
    m_ignoreStop.store(false, std::memory_order_seq_cst);
}

// ---------------------------------------------------------------------------
// Path planning
// ---------------------------------------------------------------------------

std::vector<Bot::Pos> Bot::TraversalOrder() const {
    std::vector<Pos> order;
    order.reserve(GRID_SIZE * GRID_SIZE);

    // Snake-pattern row traversal: odd rows go left-to-right, even rows right-to-left.
    for (int y = 1; y <= GRID_SIZE; ++y) {
        if (y % 2 == 1) {
            for (int x = 1; x <= GRID_SIZE; ++x) {
                if (x == WATER_X && y == WATER_Y) continue;
                order.emplace_back(x, y);
            }
        } else {
            for (int x = GRID_SIZE; x >= 1; --x) {
                if (x == WATER_X && y == WATER_Y) continue;
                order.emplace_back(x, y);
            }
        }
    }

    // Return to the start position as the final target.
    order.emplace_back(START_X, START_Y);
    return order;
}

std::vector<Bot::Pos> Bot::PathBetween(Pos start, Pos goal) const {
    if (start == goal) return {start};

    std::deque<Pos> queue;
    // cameFrom[n] = the predecessor of n in the BFS tree.
    // The start node is seeded with a sentinel {-1,-1}; reconstruction
    // stops when it reaches start (where cameFrom gives the sentinel).
    std::unordered_map<Pos, Pos, PairHash> cameFrom;

    queue.push_back(start);
    cameFrom[start] = {-1, -1};  // sentinel – no predecessor

    while (!queue.empty()) {
        Pos node = queue.front();
        queue.pop_front();

        if (node == goal) break;

        for (const auto& nb : Neighbors(node)) {
            if (cameFrom.find(nb) == cameFrom.end()) {
                cameFrom[nb] = node;
                queue.push_back(nb);
            }
        }
    }

    // goal not reachable – return a single-element path at start.
    if (cameFrom.find(goal) == cameFrom.end()) return {start};

    // Walk backwards from goal to start to reconstruct the path.
    std::vector<Pos> path;
    Pos cur = goal;
    while (cur != start) {
        path.push_back(cur);
        cur = cameFrom[cur];
    }
    path.push_back(start);
    std::reverse(path.begin(), path.end());
    return path;
}

std::vector<Bot::Pos> Bot::Neighbors(Pos node) const {
    static constexpr int kDirs[4][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};
    std::vector<Pos> result;
    result.reserve(4);

    for (const auto& d : kDirs) {
        int nx = node.first  + d[0];
        int ny = node.second + d[1];

        // Skip the impassable water block.
        if (nx == WATER_X && ny == WATER_Y) continue;

        // Allow the start position and any in-bounds grid cell.
        if ((nx == START_X && ny == START_Y) ||
            (nx >= 1 && nx <= GRID_SIZE && ny >= 1 && ny <= GRID_SIZE)) {
            result.emplace_back(nx, ny);
        }
    }
    return result;
}

// ---------------------------------------------------------------------------
// Low-level Win32 input wrappers
// ---------------------------------------------------------------------------

void Bot::PressKey(WORD vk) {
    INPUT in{};
    in.type   = INPUT_KEYBOARD;
    in.ki.wVk = vk;
    SendInput(1, &in, sizeof(INPUT));
}

void Bot::ReleaseKey(WORD vk) {
    INPUT in{};
    in.type       = INPUT_KEYBOARD;
    in.ki.wVk     = vk;
    in.ki.dwFlags = KEYEVENTF_KEYUP;
    SendInput(1, &in, sizeof(INPUT));
}

void Bot::MouseDown(DWORD flags) {
    INPUT in{};
    in.type       = INPUT_MOUSE;
    in.mi.dwFlags = flags;
    SendInput(1, &in, sizeof(INPUT));
}

void Bot::MouseUp(DWORD flags) {
    INPUT in{};
    in.type       = INPUT_MOUSE;
    in.mi.dwFlags = flags;
    SendInput(1, &in, sizeof(INPUT));
}

void Bot::MouseClick(DWORD downFlags, DWORD upFlags) {
    INPUT ins[2]{};
    ins[0].type       = INPUT_MOUSE;
    ins[0].mi.dwFlags = downFlags;
    ins[1].type       = INPUT_MOUSE;
    ins[1].mi.dwFlags = upFlags;
    SendInput(2, ins, sizeof(INPUT));
}

// ---------------------------------------------------------------------------
// Timing helper
// ---------------------------------------------------------------------------

bool Bot::WaitOrStop(std::atomic<bool>& stop, double seconds) {
    using Clock = std::chrono::steady_clock;
    auto end = Clock::now() +
               std::chrono::duration_cast<Clock::duration>(
                   std::chrono::duration<double>(seconds));

    // Poll every 10 ms so we react to the stop flag within one poll interval.
    constexpr DWORD kPollMs = 10;
    while (Clock::now() < end) {
        if (stop.load(std::memory_order_acquire)) return true;
        Sleep(kPollMs);
    }
    return stop.load(std::memory_order_acquire);
}
