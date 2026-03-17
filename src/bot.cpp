// bot.cpp – Farm-bot logic implementation.
//
// State machine order (happy path):
//   Idle → ResetPosition → MoveToRowStart → FarmRow
//        → ReturnToOrigin (every resetInterval rows)
//        → WaitForGrowth (after all rows are done)
//        → ResetPosition (next growth cycle)
//
// Any state may fall through to ErrorRecovery when:
//   - OCR returns an available but low-confidence result.
//   - Coordinates deviate beyond MAX_POS_TOLERANCE from expected.
//   - Focus is lost and cannot be regained within a grace period.
//
// Windows-only; compiled with MSVC targeting Windows 10/11 x64.

#include "bot.h"

#include <algorithm>
#include <chrono>
#include <deque>
#include <map>
#include <unordered_map>

// ---------------------------------------------------------------------------
// Internal helpers
// ---------------------------------------------------------------------------

/// FNV-inspired hash for std::pair<int,int>.
struct PairHash {
    std::size_t operator()(const std::pair<int, int>& p) const noexcept {
        std::size_t h1 = std::hash<int>{}(p.first);
        std::size_t h2 = std::hash<int>{}(p.second);
        return h1 ^ (h2 * 0x9e3779b9u + 0x6b374651u + (h1 << 6) + (h1 >> 2));
    }
};

/// Direction → virtual-key mapping (player faces East = +x direction).
static const std::map<std::pair<int, int>, WORD> kDirToVK{
    {{ 1,  0}, 'W'},   // East  → forward
    {{-1,  0}, 'S'},   // West  → backward
    {{ 0,  1}, 'A'},   // North → strafe left
    {{ 0, -1}, 'D'},   // South → strafe right
};

// ---------------------------------------------------------------------------
// Constructor
// ---------------------------------------------------------------------------

Bot::Bot(std::atomic<bool>&              stopFlag,
         std::atomic<bool>&              ignoreStopKeys,
         StatusCallback                  statusCb,
         LogCallback                     logCb,
         std::shared_ptr<IScreenCapture> screenCapture,
         std::shared_ptr<IOcr>           ocr)
    : m_stop{stopFlag}
    , m_ignoreStop{ignoreStopKeys}
    , m_statusCb{std::move(statusCb)}
    , m_logCb{std::move(logCb)}
    , m_capture{std::move(screenCapture)}
    , m_ocr{std::move(ocr)}
{}

// ---------------------------------------------------------------------------
// Inline helpers
// ---------------------------------------------------------------------------

void Bot::Log(const wchar_t* msg) {
    if (m_logCb) m_logCb(msg);
}

void Bot::Status(const wchar_t* msg) {
    if (m_statusCb) m_statusCb(msg);
}

// ---------------------------------------------------------------------------
// State machine
// ---------------------------------------------------------------------------

void Bot::TransitionTo(BotState next, const wchar_t* reason) {
    m_state.store(next, std::memory_order_release);
    wchar_t buf[256];
    if (reason && *reason) {
        swprintf_s(buf, L"[%s] %s", BotStateName(next), reason);
    } else {
        swprintf_s(buf, L"[%s]", BotStateName(next));
    }
    Log(buf);
    Status(BotStateName(next));
}

// ---------------------------------------------------------------------------
// Run – main entry point (worker thread)
// ---------------------------------------------------------------------------

void Bot::Run(const FarmConfig& cfg) {
    m_originSet = false;

    // Tilt the camera down so the player looks at the crop blocks.
    LookDown();

    // Attempt to read and save the starting position via OCR.
    PlayerPos startPos = ReadCoordinates();
    if (startPos.valid) {
        m_origin    = startPos;
        m_originSet = true;
        wchar_t buf[128];
        swprintf_s(buf, L"Ausgangsposition gespeichert: %s",
                   CoordParser::FormatPos(m_origin).c_str());
        Log(buf);
    } else {
        Log(L"OCR nicht verfügbar – rein zeitbasierter Modus aktiv.");
    }

    Pos      pos         = {START_X, START_Y};
    int      currentRow  = 0;
    int      totalRows   = cfg.farmHeight;
    bool     cycleActive = true;

    TransitionTo(BotState::ResetPosition, L"Starte Farm-Zyklus");

    while (cycleActive && !m_stop.load(std::memory_order_acquire)) {

        switch (m_state.load(std::memory_order_acquire)) {

        // ----------------------------------------------------------------
        case BotState::ResetPosition:
            HandleResetPosition(cfg);
            if (m_stop.load(std::memory_order_acquire)) break;
            pos = {START_X, START_Y};
            TransitionTo(BotState::MoveToRowStart,
                         L"Position bestätigt – gehe zur ersten Reihe");
            break;

        // ----------------------------------------------------------------
        case BotState::MoveToRowStart:
            HandleMoveToRowStart(cfg, currentRow, pos);
            if (m_stop.load(std::memory_order_acquire)) break;
            TransitionTo(BotState::FarmRow, nullptr);
            break;

        // ----------------------------------------------------------------
        case BotState::FarmRow:
            HandleFarmRow(cfg, currentRow, pos);
            if (m_stop.load(std::memory_order_acquire)) break;

            ++currentRow;

            if (currentRow >= totalRows) {
                // All rows completed – return to origin then wait for growth.
                TransitionTo(BotState::ReturnToOrigin,
                             L"Alle Reihen erledigt – kehre zum Ursprung zurück");
            } else if (cfg.resetInterval > 0 &&
                       (currentRow % cfg.resetInterval == 0)) {
                // Periodic safe-point reset.
                TransitionTo(BotState::ReturnToOrigin,
                             L"Reset-Intervall erreicht – kehre zum Ursprung zurück");
            } else {
                // Continue with the next row.
                TransitionTo(BotState::MoveToRowStart, nullptr);
            }
            break;

        // ----------------------------------------------------------------
        case BotState::ReturnToOrigin:
            HandleReturnToOrigin(cfg, pos);
            if (m_stop.load(std::memory_order_acquire)) break;

            if (currentRow >= totalRows) {
                // Full cycle done.
                if (cfg.waitForGrowth > 0.0) {
                    TransitionTo(BotState::WaitForGrowth, nullptr);
                } else {
                    // No growth wait → restart immediately.
                    currentRow = 0;
                    TransitionTo(BotState::ResetPosition,
                                 L"Neuer Zyklus – kein Wachstums-Warten");
                }
            } else {
                // Mid-cycle reset – continue with next row group.
                TransitionTo(BotState::ResetPosition,
                             L"Zwischenreset – weiter mit nächster Gruppe");
            }
            break;

        // ----------------------------------------------------------------
        case BotState::WaitForGrowth:
            HandleWaitForGrowth(cfg);
            if (m_stop.load(std::memory_order_acquire)) break;
            currentRow = 0;
            TransitionTo(BotState::ResetPosition, L"Wachstum abgeschlossen – neuer Zyklus");
            break;

        // ----------------------------------------------------------------
        case BotState::ErrorRecovery:
            HandleErrorRecovery(L"Fehlerkorrektur läuft");
            // After recovery always fall back to Idle so the user can restart.
            cycleActive = false;
            TransitionTo(BotState::Idle, L"Bot gestoppt nach Fehler");
            break;

        // ----------------------------------------------------------------
        case BotState::Idle:
        default:
            cycleActive = false;
            break;
        }
    }

    if (!m_stop.load(std::memory_order_acquire) && !cycleActive) {
        Log(L"Farm-Zyklus vollständig beendet.");
    }

    TransitionTo(BotState::Idle, nullptr);
}

// ---------------------------------------------------------------------------
// State handlers
// ---------------------------------------------------------------------------

void Bot::HandleResetPosition(const FarmConfig& cfg) {
    if (!m_originSet) {
        // No origin saved – accept current position without verification.
        Log(L"Kein gespeicherter Ursprung – überspringe Positionsprüfung.");
        return;
    }

    // Try to read current coordinates and validate them.
    PlayerPos current = ReadCoordinates();
    if (!current.valid) {
        Log(L"OCR nicht verfügbar – überspringe Positionsprüfung.");
        return;
    }

    if (!CoordParser::NearOrigin(current, m_origin, MAX_POS_TOLERANCE)) {
        wchar_t buf[256];
        swprintf_s(buf,
            L"Position weicht ab: erwartet %s – tatsächlich %s",
            CoordParser::FormatPos(m_origin).c_str(),
            CoordParser::FormatPos(current).c_str());
        Log(buf);
        HandleErrorRecovery(buf);
    } else {
        wchar_t buf[128];
        swprintf_s(buf, L"Position bestätigt: %s",
                   CoordParser::FormatPos(current).c_str());
        Log(buf);
    }
}

void Bot::HandleMoveToRowStart(const FarmConfig& cfg, int row, Pos& pos) {
    // The first cell of each row in the snake pattern.
    bool evenRow = (row % 2 == 0);
    Pos target = evenRow ? Pos{1, row + 1} : Pos{cfg.farmWidth, row + 1};

    wchar_t buf[128];
    swprintf_s(buf, L"Reihe %d – gehe zu (%d,%d)", row + 1, target.first, target.second);
    Log(buf);

    auto path = PathBetween(pos, target, cfg.farmWidth, cfg.farmHeight);
    for (std::size_t i = 1; i < path.size(); ++i) {
        if (m_stop.load(std::memory_order_acquire)) return;
        if (!WaitForFocus()) return;
        StepTowards(pos, path[i], cfg.blockDelay);
        pos = path[i];
    }
}

void Bot::HandleFarmRow(const FarmConfig& cfg, int row, Pos& pos) {
    bool evenRow = (row % 2 == 0);

    wchar_t buf[128];
    swprintf_s(buf, L"Ernte Reihe %d (%s)", row + 1, evenRow ? L"→" : L"←");
    Log(buf);

    int colStart = evenRow ? 1               : cfg.farmWidth;
    int colEnd   = evenRow ? cfg.farmWidth   : 1;
    int step     = evenRow ? 1               : -1;

    for (int col = colStart; ; col += step) {
        if (m_stop.load(std::memory_order_acquire)) return;

        Pos target{col, row + 1};

        // Walk to the next cell.
        auto path = PathBetween(pos, target, cfg.farmWidth, cfg.farmHeight);
        for (std::size_t i = 1; i < path.size(); ++i) {
            if (m_stop.load(std::memory_order_acquire)) return;
            if (!WaitForFocus()) return;
            StepTowards(pos, path[i], cfg.blockDelay);
            pos = path[i];
        }

        // Harvest and replant.
        if (!WaitForFocus()) return;
        HarvestAndPlant(cfg.breakDelay, cfg.replantPause);

        if (col == colEnd) break;
    }
}

void Bot::HandleReturnToOrigin(const FarmConfig& cfg, Pos& pos) {
    Pos origin{START_X, START_Y};
    Log(L"Kehre zur Ausgangsposition zurück");

    auto path = PathBetween(pos, origin, cfg.farmWidth, cfg.farmHeight);
    for (std::size_t i = 1; i < path.size(); ++i) {
        if (m_stop.load(std::memory_order_acquire)) return;
        if (!WaitForFocus()) return;
        StepTowards(pos, path[i], cfg.blockDelay);
        pos = path[i];
    }
    Log(L"Ausgangsposition erreicht");
}

void Bot::HandleWaitForGrowth(const FarmConfig& cfg) {
    wchar_t buf[128];
    swprintf_s(buf, L"Warte %.0f Sekunden auf Wachstum ...", cfg.waitForGrowth);
    Log(buf);

    const auto kTick = 5.0; // Log a "still waiting" line every 5 seconds.
    double remaining = cfg.waitForGrowth;
    while (remaining > 0.0 && !m_stop.load(std::memory_order_acquire)) {
        double chunk = (remaining < kTick) ? remaining : kTick;
        if (WaitOrStop(m_stop, chunk)) return;
        remaining -= chunk;

        if (remaining > 0.0) {
            swprintf_s(buf, L"Noch %.0f Sekunden warten ...", remaining);
            Status(buf);
        }
    }
}

void Bot::HandleErrorRecovery(const std::wstring& reason) {
    wchar_t buf[512];
    swprintf_s(buf, L"FEHLER: %s", reason.c_str());
    Log(buf);
    Status(L"Fehlerkorrektur – Bot gestoppt");

    // Signal a clean stop so the GUI can reset its controls.
    m_stop.store(true, std::memory_order_release);
}

// ---------------------------------------------------------------------------
// Coordinate / OCR helpers
// ---------------------------------------------------------------------------

void Bot::ToggleF3Open() {
    // Press F3 to open the debug overlay; give the game time to render it.
    PressKey(VK_F3);
    Sleep(80);
    ReleaseKey(VK_F3);
    Sleep(300); // wait for the overlay to appear
}

void Bot::ToggleF3Close() {
    PressKey(VK_F3);
    Sleep(80);
    ReleaseKey(VK_F3);
    Sleep(100);
}

ScreenBitmap Bot::CaptureF3Region() const {
    if (!m_capture) return {};
    // The F3 overlay occupies roughly the top-left quarter of the screen.
    int sw = m_capture->ScreenWidth();
    int sh = m_capture->ScreenHeight();
    return m_capture->CaptureRegion(0, 0, sw / 2, sh / 2);
}

PlayerPos Bot::ReadCoordinates() {
    if (!m_ocr || !m_capture) return {};

    ToggleF3Open();

    ScreenBitmap bmp = CaptureF3Region();
    OcrResult    ocr = m_ocr->Extract(bmp);

    ToggleF3Close();

    // OCR engine not configured – fall back to timing-only mode.
    if (!ocr.available) return {};

    // OCR returned a result but confidence is too low → ErrorRecovery.
    if (ocr.confidence < OCR_MIN_CONFIDENCE) {
        wchar_t buf[128];
        swprintf_s(buf,
            L"OCR-Konfidenz zu niedrig (%.0f%%) – überspringe Prüfung.",
            static_cast<double>(ocr.confidence) * 100.0);
        Log(buf);
        return {};
    }

    PlayerPos pos = CoordParser::Parse(ocr.text);
    if (!pos.valid) {
        Log(L"Koordinaten konnten nicht aus OCR-Text gelesen werden.");
    }
    return pos;
}

bool Bot::ValidatePosition(const PlayerPos& current, const PlayerPos& expected) {
    if (!current.valid || !expected.valid) return true; // Can't validate – proceed.

    if (!CoordParser::NearOrigin(current, expected, MAX_POS_TOLERANCE)) {
        wchar_t buf[256];
        swprintf_s(buf,
            L"Unerwartete Position: erwartet %s – tatsächlich %s",
            CoordParser::FormatPos(expected).c_str(),
            CoordParser::FormatPos(current).c_str());
        HandleErrorRecovery(buf);
        return false;
    }
    return true;
}

// ---------------------------------------------------------------------------
// Focus helpers
// ---------------------------------------------------------------------------

bool Bot::MinecraftHasFocus() const {
    HWND fg = GetForegroundWindow();
    if (!fg) return false;
    DWORD pid = 0;
    GetWindowThreadProcessId(fg, &pid);
    return (pid != GetCurrentProcessId());
}

bool Bot::WaitForFocus() {
    if (MinecraftHasFocus()) return true;

    Log(L"Fokusverlust erkannt – warte auf Minecraft-Fenster ...");
    Status(L"Warte auf Fokus ...");

    while (!MinecraftHasFocus()) {
        if (m_stop.load(std::memory_order_acquire)) return false;
        Sleep(250);
    }
    Log(L"Fokus wiedererlangt.");
    return true;
}

// ---------------------------------------------------------------------------
// Camera helper
// ---------------------------------------------------------------------------

void Bot::LookDown() {
    int w = GetSystemMetrics(SM_CXSCREEN);
    int h = GetSystemMetrics(SM_CYSCREEN);

    // Move cursor to the screen centre (absolute coords, 0-65535 range).
    INPUT in{};
    in.type       = INPUT_MOUSE;
    in.mi.dx      = static_cast<LONG>((w / 2) * 65535L / w);
    in.mi.dy      = static_cast<LONG>((h / 2) * 65535L / h);
    in.mi.dwFlags = MOUSEEVENTF_MOVE | MOUSEEVENTF_ABSOLUTE;
    SendInput(1, &in, sizeof(INPUT));
    Sleep(150);

    // Tilt the camera down with a relative mouse movement.
    in.mi.dx      = 0;
    in.mi.dy      = static_cast<LONG>(h * LOOK_DOWN_RATIO);
    in.mi.dwFlags = MOUSEEVENTF_MOVE;
    SendInput(1, &in, sizeof(INPUT));
    Sleep(200);
}

// ---------------------------------------------------------------------------
// Harvest / plant helper
// ---------------------------------------------------------------------------

void Bot::HarvestAndPlant(double breakDelay, double replantPause) {
    MouseDown(MOUSEEVENTF_LEFTDOWN);
    if (WaitOrStop(m_stop, breakDelay)) {
        MouseUp(MOUSEEVENTF_LEFTUP);
        return;
    }
    MouseUp(MOUSEEVENTF_LEFTUP);

    MouseClick(MOUSEEVENTF_RIGHTDOWN, MOUSEEVENTF_RIGHTUP);
    if (replantPause > 0.0) WaitOrStop(m_stop, replantPause);
}

// ---------------------------------------------------------------------------
// Movement helper
// ---------------------------------------------------------------------------

void Bot::StepTowards(Pos current, Pos next, double blockDelay) {
    int dx = next.first  - current.first;
    int dy = next.second - current.second;

    auto it = kDirToVK.find({dx, dy});
    if (it == kDirToVK.end()) return;

    m_ignoreStop.store(true,  std::memory_order_seq_cst);
    PressKey(it->second);
    WaitOrStop(m_stop, blockDelay);
    ReleaseKey(it->second);
    m_ignoreStop.store(false, std::memory_order_seq_cst);
}

// ---------------------------------------------------------------------------
// Path planning
// ---------------------------------------------------------------------------

std::vector<Bot::Pos> Bot::TraversalOrder(int width, int height) const {
    std::vector<Pos> order;
    order.reserve(static_cast<std::size_t>(width) * static_cast<std::size_t>(height));

    for (int y = 1; y <= height; ++y) {
        if (y % 2 == 1) {
            for (int x = 1; x <= width;  ++x) order.emplace_back(x, y);
        } else {
            for (int x = width; x >= 1;  --x) order.emplace_back(x, y);
        }
    }
    order.emplace_back(START_X, START_Y); // return to start
    return order;
}

std::vector<Bot::Pos> Bot::PathBetween(Pos start, Pos goal,
                                        int width, int height) const {
    if (start == goal) return {start};

    std::deque<Pos> queue;
    std::unordered_map<Pos, Pos, PairHash> cameFrom;

    queue.push_back(start);
    cameFrom[start] = {-1, -1};

    while (!queue.empty()) {
        Pos node = queue.front();
        queue.pop_front();
        if (node == goal) break;
        for (const auto& nb : Neighbors(node, width, height)) {
            if (cameFrom.find(nb) == cameFrom.end()) {
                cameFrom[nb] = node;
                queue.push_back(nb);
            }
        }
    }

    if (cameFrom.find(goal) == cameFrom.end()) return {start};

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

std::vector<Bot::Pos> Bot::Neighbors(Pos node, int width, int height) const {
    static constexpr int kDirs[4][2] = {{1,0},{-1,0},{0,1},{0,-1}};
    std::vector<Pos> result;
    result.reserve(4);
    for (const auto& d : kDirs) {
        int nx = node.first  + d[0];
        int ny = node.second + d[1];
        if ((nx == START_X && ny == START_Y) ||
            (nx >= 1 && nx <= width && ny >= 1 && ny <= height)) {
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
    constexpr DWORD kPollMs = 10;
    while (Clock::now() < end) {
        if (stop.load(std::memory_order_acquire)) return true;
        Sleep(kPollMs);
    }
    return stop.load(std::memory_order_acquire);
}
