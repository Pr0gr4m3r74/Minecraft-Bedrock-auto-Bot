// gui.cpp – Win32 main-window creation and message handling.
//
// Layout (logical pixels at 96 DPI):
//   - Nine configuration rows (labels + edit boxes)
//   - Start / Stop buttons
//   - Status label
//   - Scrollable log list-box
//   - Install / Uninstall buttons (bottom)
//
// A global hotkey (default F6) starts the bot when idle; any key press
// stops it during a run (WH_KEYBOARD_LL safety hook).

#include "gui.h"
#include "bot.h"
#include "screen_capture.h"
#include "ocr.h"
#include "utils.h"

#include <commctrl.h>
#include <optional>
#include <memory>
#include <string>

// ---------------------------------------------------------------------------
// Layout constants (logical pixels @ 96 DPI)
// ---------------------------------------------------------------------------
static constexpr int kWinClientW = 500;
static constexpr int kWinClientH = 600;
static constexpr int kMx         = 12;
static constexpr int kMy         = 10;
static constexpr int kRowH       = 22;
static constexpr int kRowGap     = 5;
static constexpr int kLabelW     = 230;
static constexpr int kEditW      = 90;
static constexpr int kBtnW       = 130;
static constexpr int kBtnH       = 26;
static constexpr int kBtnGap     = 8;
static constexpr int kLogH       = 150;
static constexpr int kMaxLogLines = 300;

// ---------------------------------------------------------------------------
// Module-level state pointer (WH_KEYBOARD_LL has no user-data parameter)
// ---------------------------------------------------------------------------
static AppState* g_state = nullptr;

// ---------------------------------------------------------------------------
// Low-level keyboard hook – any key stops the bot while it is running
// ---------------------------------------------------------------------------
static LRESULT CALLBACK LowLevelKeyboardProc(int nCode, WPARAM wParam, LPARAM lParam) {
    if (nCode == HC_ACTION &&
        (wParam == WM_KEYDOWN || wParam == WM_SYSKEYDOWN)) {
        if (g_state &&
            !g_state->ignoreStopKeys.load(std::memory_order_acquire)) {
            g_state->stopFlag.store(true, std::memory_order_release);
            PostMessage(g_state->hwnd, WM_APP_STOP_KEY, 0, 0);
        }
    }
    return CallNextHookEx(nullptr, nCode, wParam, lParam);
}

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static std::wstring GetEditText(HWND hEdit) {
    int len = GetWindowTextLengthW(hEdit);
    if (len <= 0) return {};
    std::wstring s(static_cast<std::size_t>(len), L'\0');
    GetWindowTextW(hEdit, s.data(), len + 1);
    return s;
}

/// Parse the hotkey edit box: accept "F1"..."F24" or a single letter/digit.
static UINT ParseHotkeyText(const std::wstring& s) {
    if (s.empty()) return VK_F6;
    if ((s[0] == L'F' || s[0] == L'f') && s.size() >= 2) {
        try {
            int n = std::stoi(s.substr(1));
            if (n >= 1 && n <= 24)
                return VK_F1 + static_cast<UINT>(n - 1);
        } catch (...) {}
    }
    if (s.size() == 1) {
        return static_cast<UINT>(std::towupper(s[0]));
    }
    return VK_F6;
}

static void ToggleControls(AppState* state, bool running) {
    BOOL on = running ? FALSE : TRUE;
    EnableWindow(state->hEditFarmWidth,     on);
    EnableWindow(state->hEditFarmHeight,    on);
    EnableWindow(state->hEditBlockDelay,    on);
    EnableWindow(state->hEditBreakDelay,    on);
    EnableWindow(state->hEditReplant,       on);
    EnableWindow(state->hEditWaitGrowth,    on);
    EnableWindow(state->hEditResetInterval, on);
    EnableWindow(state->hEditHotkey,        on);
    EnableWindow(state->hEditCountdown,     on);
    EnableWindow(state->hBtnStart,          on);
    EnableWindow(state->hBtnInstall,        on);
    EnableWindow(state->hBtnUninstall,      on);
    EnableWindow(state->hBtnStop, running ? TRUE : FALSE);
}

/// Thread-safe status update.
static void PostStatus(AppState* state, const wchar_t* text) {
    auto* msg = new std::wstring(text);
    if (!PostMessage(state->hwnd, WM_APP_STATUS, 0,
                     reinterpret_cast<LPARAM>(msg))) {
        delete msg;
    }
}

/// Thread-safe log append.
static void PostLog(AppState* state, const wchar_t* text) {
    auto* msg = new std::wstring(text);
    if (!PostMessage(state->hwnd, WM_APP_LOG, 0,
                     reinterpret_cast<LPARAM>(msg))) {
        delete msg;
    }
}

/// Append a line to the log list-box (GUI thread only).
static void AppendLog(AppState* state, const wchar_t* text) {
    int count = static_cast<int>(
        SendMessageW(state->hListLog, LB_GETCOUNT, 0, 0));
    if (count >= kMaxLogLines) {
        SendMessageW(state->hListLog, LB_DELETESTRING, 0, 0);
        --count;
    }
    int idx = static_cast<int>(
        SendMessageW(state->hListLog, LB_ADDSTRING, 0,
                     reinterpret_cast<LPARAM>(text)));
    if (idx >= 0) {
        SendMessageW(state->hListLog, LB_SETTOPINDEX,
                     static_cast<WPARAM>(idx), 0);
    }
}

// ---------------------------------------------------------------------------
// Global hotkey management
// ---------------------------------------------------------------------------
static void RegisterToggleHotkey(AppState* state, UINT vk) {
    if (state->registeredHotkeyVk) {
        UnregisterHotKey(state->hwnd, ID_HOTKEY_TOGGLE);
        state->registeredHotkeyVk = 0;
    }
    if (vk && RegisterHotKey(state->hwnd, ID_HOTKEY_TOGGLE, 0, vk)) {
        state->registeredHotkeyVk = vk;
    }
}

// ---------------------------------------------------------------------------
// Action handlers
// ---------------------------------------------------------------------------

static void CleanupAfterRun(HWND hwnd, AppState* state, bool stopped) {
    state->stopFlag.store(true, std::memory_order_release);

    if (state->keyboardHook) {
        UnhookWindowsHookEx(state->keyboardHook);
        state->keyboardHook = nullptr;
    }

    KillTimer(hwnd, IDT_COUNTDOWN);
    ToggleControls(state, false);

    // Re-register the hotkey so it can start the next run.
    RegisterToggleHotkey(state, state->botStartStopVk);

    if (stopped) {
        wchar_t buf[128]{};
        GetWindowTextW(state->hStaticStatus, buf, _countof(buf));
        if (wcsncmp(buf, L"Fertig", 6) != 0) {
            SetWindowTextW(state->hStaticStatus, L"Bereit");
        }
    }
}

static void HandleStop(HWND hwnd, AppState* state) {
    SetWindowTextW(state->hStaticStatus, L"Stop angefordert");
    state->stopFlag.store(true, std::memory_order_release);
    KillTimer(hwnd, IDT_COUNTDOWN);
}

static void LaunchWorker(AppState* state) {
    if (state->worker.joinable()) state->worker.join();

    FarmConfig cfg;
    cfg.farmWidth     = state->botFarmWidth;
    cfg.farmHeight    = state->botFarmHeight;
    cfg.blockDelay    = state->botBlockDelay;
    cfg.breakDelay    = state->botBreakDelay;
    cfg.replantPause  = state->botReplantPause;
    cfg.waitForGrowth = state->botWaitForGrowth;
    cfg.resetInterval = state->botResetInterval;
    cfg.startStopVk   = state->botStartStopVk;

    // Replace NullOcr with a concrete engine to enable coordinate validation.
    auto capture = std::make_shared<GdiScreenCapture>();
    auto ocr     = std::make_shared<NullOcr>();

    state->worker = std::thread([state, cfg,
                                  cap = std::move(capture),
                                  o   = std::move(ocr)]() mutable {
        Bot bot(
            state->stopFlag,
            state->ignoreStopKeys,
            [state](const wchar_t* txt) { PostStatus(state, txt); },
            [state](const wchar_t* txt) { PostLog   (state, txt); },
            std::move(cap),
            std::move(o));

        bot.Run(cfg);

        bool stopped = state->stopFlag.load(std::memory_order_acquire);
        PostMessage(state->hwnd, WM_APP_RUN_DONE,
                    stopped ? 1u : 0u, 0);
    });
}

static void HandleStart(HWND hwnd, AppState* state) {
    auto parseInt = [&](HWND hEdit, int minVal) -> std::optional<int> {
        auto v = Utils::ParseDouble(GetEditText(hEdit));
        if (!v) return std::nullopt;
        return std::max(minVal, static_cast<int>(*v));
    };
    auto parseDouble = [&](HWND hEdit, double minVal) -> std::optional<double> {
        auto v = Utils::ParseDouble(GetEditText(hEdit));
        if (!v) return std::nullopt;
        return std::max(minVal, *v);
    };

    auto optFarmWidth     = parseInt   (state->hEditFarmWidth,      1);
    auto optFarmHeight    = parseInt   (state->hEditFarmHeight,     1);
    auto optBlockDelay    = parseDouble(state->hEditBlockDelay,     0.05);
    auto optBreakDelay    = parseDouble(state->hEditBreakDelay,     0.10);
    auto optReplant       = parseDouble(state->hEditReplant,        0.0);
    auto optWaitGrowth    = parseDouble(state->hEditWaitGrowth,     0.0);
    auto optResetInterval = parseInt   (state->hEditResetInterval,  0);
    auto optCountdown     = parseDouble(state->hEditCountdown,      0.0);

    if (!optFarmWidth  || !optFarmHeight   || !optBlockDelay  ||
        !optBreakDelay || !optReplant      || !optWaitGrowth  ||
        !optResetInterval || !optCountdown) {
        MessageBoxW(hwnd, L"Bitte gueltige Zahlen eingeben.",
                    L"Eingabefehler", MB_OK | MB_ICONERROR);
        return;
    }

    UINT vk = ParseHotkeyText(GetEditText(state->hEditHotkey));

    if (MessageBoxW(hwnd,
            L"Der Bot uebernimmt Tastatur und Maus.\n"
            L"Stelle sicher, dass Minecraft im Vordergrund ist.",
            L"Warnung", MB_OKCANCEL | MB_ICONWARNING) != IDOK) {
        return;
    }

    state->botFarmWidth     = *optFarmWidth;
    state->botFarmHeight    = *optFarmHeight;
    state->botBlockDelay    = *optBlockDelay;
    state->botBreakDelay    = *optBreakDelay;
    state->botReplantPause  = *optReplant;
    state->botWaitForGrowth = *optWaitGrowth;
    state->botResetInterval = *optResetInterval;
    state->botStartStopVk   = vk;

    ToggleControls(state, true);
    state->stopFlag.store(false, std::memory_order_release);

    // Unregister toggle hotkey while running (keyboard hook handles stops).
    if (state->registeredHotkeyVk) {
        UnregisterHotKey(hwnd, ID_HOTKEY_TOGGLE);
        state->registeredHotkeyVk = 0;
    }

    if (state->keyboardHook) UnhookWindowsHookEx(state->keyboardHook);
    state->keyboardHook = SetWindowsHookExW(
        WH_KEYBOARD_LL, LowLevelKeyboardProc, nullptr, 0);

    double countdown = *optCountdown;
    state->countdownEndTick =
        GetTickCount64() + static_cast<ULONGLONG>(countdown * 1000.0);

    wchar_t buf[64];
    swprintf_s(buf, L"Countdown %.1fs ...", countdown);
    SetWindowTextW(state->hStaticStatus, buf);
    SetTimer(hwnd, IDT_COUNTDOWN, 100, nullptr);
}

// ---------------------------------------------------------------------------
// Window procedure
// ---------------------------------------------------------------------------

static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    auto* state = reinterpret_cast<AppState*>(
        GetWindowLongPtrW(hwnd, GWLP_USERDATA));

    switch (msg) {

    // -----------------------------------------------------------------------
    case WM_CREATE: {
        auto* cs = reinterpret_cast<CREATESTRUCTW*>(lParam);
        state = reinterpret_cast<AppState*>(cs->lpCreateParams);
        state->hwnd = hwnd;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA,
                          reinterpret_cast<LONG_PTR>(state));

        HFONT hFont = reinterpret_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));

        auto MakeLabel = [&](int x, int y, int w, int h,
                              const wchar_t* text) -> HWND {
            HWND h_ = CreateWindowExW(
                0, L"STATIC", text, WS_CHILD | WS_VISIBLE | SS_LEFT,
                x, y, w, h, hwnd, nullptr, cs->hInstance, nullptr);
            SendMessageW(h_, WM_SETFONT, reinterpret_cast<WPARAM>(hFont), TRUE);
            return h_;
        };

        auto MakeEdit = [&](int x, int y, int w, int h,
                             const wchar_t* init, int id) -> HWND {
            HWND h_ = CreateWindowExW(
                WS_EX_CLIENTEDGE, L"EDIT", init,
                WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL,
                x, y, w, h, hwnd,
                reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),
                cs->hInstance, nullptr);
            SendMessageW(h_, WM_SETFONT, reinterpret_cast<WPARAM>(hFont), TRUE);
            return h_;
        };

        auto MakeButton = [&](int x, int y, int w, int h,
                               const wchar_t* text, int id) -> HWND {
            HWND h_ = CreateWindowExW(
                0, L"BUTTON", text,
                WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                x, y, w, h, hwnd,
                reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),
                cs->hInstance, nullptr);
            SendMessageW(h_, WM_SETFONT, reinterpret_cast<WPARAM>(hFont), TRUE);
            return h_;
        };

        int y     = kMy;
        int editX = kMx + kLabelW + kBtnGap;

        // Row 0 – Farm-Breite
        MakeLabel(kMx, y + 3, kLabelW, kRowH, L"Farm-Breite (Bloecke)");
        state->hEditFarmWidth = MakeEdit(editX, y, kEditW, kRowH, L"9", IDC_EDIT_FARMWIDTH);
        y += kRowH + kRowGap;

        // Row 1 – Farm-Hoehe
        MakeLabel(kMx, y + 3, kLabelW, kRowH, L"Farm-Hoehe (Bloecke)");
        state->hEditFarmHeight = MakeEdit(editX, y, kEditW, kRowH, L"9", IDC_EDIT_FARMHEIGHT);
        y += kRowH + kRowGap;

        // Row 2 – Block-Delay
        MakeLabel(kMx, y + 3, kLabelW, kRowH, L"Block-Delay (Sek./Block)");
        state->hEditBlockDelay = MakeEdit(editX, y, kEditW, kRowH, L"0.25", IDC_EDIT_BLOCKDELAY);
        y += kRowH + kRowGap;

        // Row 3 – Abbau-Dauer
        MakeLabel(kMx, y + 3, kLabelW, kRowH, L"Abbau-Haltedauer (Sekunden)");
        state->hEditBreakDelay = MakeEdit(editX, y, kEditW, kRowH, L"0.6", IDC_EDIT_BREAKDELAY);
        y += kRowH + kRowGap;

        // Row 4 – Pflanz-Pause
        MakeLabel(kMx, y + 3, kLabelW, kRowH, L"Pause nach Pflanzen (Sek.)");
        state->hEditReplant = MakeEdit(editX, y, kEditW, kRowH, L"0.1", IDC_EDIT_REPLANT);
        y += kRowH + kRowGap;

        // Row 5 – Wartezeit
        MakeLabel(kMx, y + 3, kLabelW, kRowH, L"Wartezeit Wachstum (Sek.)");
        state->hEditWaitGrowth = MakeEdit(editX, y, kEditW, kRowH, L"300", IDC_EDIT_WAITGROWTH);
        y += kRowH + kRowGap;

        // Row 6 – Reset-Intervall
        MakeLabel(kMx, y + 3, kLabelW, kRowH, L"Reset-Intervall (Reihen, 0=aus)");
        state->hEditResetInterval = MakeEdit(editX, y, kEditW, kRowH, L"9", IDC_EDIT_RESETINTERVAL);
        y += kRowH + kRowGap;

        // Row 7 – Hotkey
        MakeLabel(kMx, y + 3, kLabelW, kRowH, L"Start/Stop-Hotkey (z. B. F6)");
        state->hEditHotkey = MakeEdit(editX, y, kEditW, kRowH, L"F6", IDC_EDIT_HOTKEY);
        y += kRowH + kRowGap;

        // Row 8 – Countdown
        MakeLabel(kMx, y + 3, kLabelW, kRowH, L"Countdown (Sekunden)");
        state->hEditCountdown = MakeEdit(editX, y, kEditW, kRowH, L"3", IDC_EDIT_COUNTDOWN);
        y += kRowH + kRowGap + 6;

        // Buttons
        state->hBtnStart = MakeButton(kMx, y, kBtnW, kBtnH,
                                      L"Start", IDC_BTN_START);
        state->hBtnStop  = MakeButton(kMx + kBtnW + kBtnGap, y,
                                      160, kBtnH,
                                      L"Stopp (beliebige Taste)", IDC_BTN_STOP);
        EnableWindow(state->hBtnStop, FALSE);
        y += kBtnH + kRowGap + 6;

        // Status line
        MakeLabel(kMx, y + 2, 52, kRowH, L"Status:");
        state->hStaticStatus = MakeLabel(
            kMx + 56, y + 2, kWinClientW - kMx - 56 - kMx, kRowH, L"Bereit");
        y += kRowH + kRowGap + 4;

        // Log heading (bold)
        {
            HWND hHeading = CreateWindowExW(
                0, L"STATIC", L"Protokoll",
                WS_CHILD | WS_VISIBLE | SS_LEFT,
                kMx, y, kWinClientW - kMx * 2, kRowH,
                hwnd, nullptr, cs->hInstance, nullptr);
            LOGFONTW lf{};
            GetObjectW(hFont, sizeof(lf), &lf);
            lf.lfWeight    = FW_BOLD;
            state->hBoldFont = CreateFontIndirectW(&lf);
            SendMessageW(hHeading, WM_SETFONT,
                         reinterpret_cast<WPARAM>(state->hBoldFont), TRUE);
        }
        y += kRowH + 2;

        // Log list-box
        state->hListLog = CreateWindowExW(
            WS_EX_CLIENTEDGE, L"LISTBOX", nullptr,
            WS_CHILD | WS_VISIBLE | WS_VSCROLL |
            LBS_NOINTEGRALHEIGHT | LBS_NOSEL,
            kMx, y, kWinClientW - kMx * 2, kLogH,
            hwnd,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_LIST_LOG)),
            cs->hInstance, nullptr);
        SendMessageW(state->hListLog, WM_SETFONT,
                     reinterpret_cast<WPARAM>(hFont), TRUE);
        y += kLogH + kRowGap + 6;

        // Install / Uninstall buttons
        state->hBtnInstall   = MakeButton(kMx, y, kBtnW, kBtnH,
                                          L"Installieren",    IDC_BTN_INSTALL);
        state->hBtnUninstall = MakeButton(kMx + kBtnW + kBtnGap, y,
                                          kBtnW, kBtnH,
                                          L"Deinstallieren",  IDC_BTN_UNINSTALL);

        // Register the default start/stop hotkey.
        RegisterToggleHotkey(state, state->botStartStopVk);

        PostMessage(hwnd, WM_APP_WELCOME, 0, 0);
        return 0;
    }

    // -----------------------------------------------------------------------
    case WM_APP_WELCOME:
        MessageBoxW(hwnd,
            L"Minecraft Bedrock Auto Bot\n\n"
            L"1) Starte Minecraft und halte das Fenster im Vordergrund.\n"
            L"2) Stelle dich auf Position (0,1), schaue nach Osten,\n"
            L"   halte die Ernte-Pflanze in der Hand.\n"
            L"3) Druecke \"Start\" oder den konfigurierten Hotkey (Standard: F6).\n"
            L"   Nach dem Countdown laeuft der Bot automatisch.\n"
            L"4) Jede beliebige Taste oder \"Stopp\" beendet den Bot sofort.\n"
            L"5) Zum Deinstallieren: diese EXE-Datei loeschen.",
            L"Anleitung", MB_OK | MB_ICONINFORMATION);
        return 0;

    // -----------------------------------------------------------------------
    case WM_HOTKEY:
        if (wParam == ID_HOTKEY_TOGGLE && state) {
            // Toggle: start if idle, stop if running.
            if (!state->stopFlag.load(std::memory_order_acquire) &&
                state->worker.joinable()) {
                // Running -> stop.
                HandleStop(hwnd, state);
            } else {
                // Idle -> start.
                HandleStart(hwnd, state);
            }
        }
        return 0;

    // -----------------------------------------------------------------------
    case WM_COMMAND:
        if (!state) break;
        switch (LOWORD(wParam)) {
        case IDC_BTN_START:
            HandleStart(hwnd, state);
            break;
        case IDC_BTN_STOP:
            HandleStop(hwnd, state);
            break;
        case IDC_BTN_INSTALL:
            MessageBoxW(hwnd,
                L"Diese Anwendung ist eigenstaendig und benoetigt\n"
                L"keine zusaetzliche Installation.",
                L"Installation", MB_OK | MB_ICONINFORMATION);
            break;
        case IDC_BTN_UNINSTALL:
            if (MessageBoxW(hwnd,
                    L"Zum Deinstallieren loeschen Sie diese EXE-Datei.\n"
                    L"Fortfahren?",
                    L"Deinstallieren",
                    MB_YESNO | MB_ICONQUESTION) == IDYES) {
                MessageBoxW(hwnd,
                    L"Loeschen Sie diese Datei, um die Anwendung\n"
                    L"vollstaendig zu entfernen.",
                    L"Deinstallieren", MB_OK | MB_ICONINFORMATION);
            }
            break;
        }
        return 0;

    // -----------------------------------------------------------------------
    case WM_TIMER:
        if (!state || wParam != IDT_COUNTDOWN) break;
        {
            ULONGLONG now = GetTickCount64();
            if (state->stopFlag.load(std::memory_order_acquire) ||
                now >= state->countdownEndTick) {
                KillTimer(hwnd, IDT_COUNTDOWN);
                if (!state->stopFlag.load(std::memory_order_acquire)) {
                    SetWindowTextW(state->hStaticStatus, L"Starte ...");
                    AppendLog(state, L">> Bot wird gestartet");
                    LaunchWorker(state);
                } else {
                    CleanupAfterRun(hwnd, state, true);
                }
            } else {
                double remaining =
                    static_cast<double>(state->countdownEndTick - now) / 1000.0;
                wchar_t buf[64];
                swprintf_s(buf, L"Countdown %.1fs ...", remaining);
                SetWindowTextW(state->hStaticStatus, buf);
            }
        }
        return 0;

    // -----------------------------------------------------------------------
    case WM_APP_STATUS:
        if (state && lParam) {
            auto* msg = reinterpret_cast<std::wstring*>(lParam);
            SetWindowTextW(state->hStaticStatus, msg->c_str());
            delete msg;
        }
        return 0;

    // -----------------------------------------------------------------------
    case WM_APP_LOG:
        if (state && lParam) {
            auto* msg = reinterpret_cast<std::wstring*>(lParam);
            AppendLog(state, msg->c_str());
            delete msg;
        }
        return 0;

    // -----------------------------------------------------------------------
    case WM_APP_RUN_DONE:
        if (state) {
            bool stopped = (wParam != 0);

            if (state->keyboardHook) {
                UnhookWindowsHookEx(state->keyboardHook);
                state->keyboardHook = nullptr;
            }

            ToggleControls(state, false);
            RegisterToggleHotkey(state, state->botStartStopVk);

            if (stopped) {
                wchar_t buf[128]{};
                GetWindowTextW(state->hStaticStatus, buf, _countof(buf));
                if (wcsncmp(buf, L"Fertig", 6) != 0) {
                    SetWindowTextW(state->hStaticStatus, L"Bereit");
                }
                AppendLog(state, L">> Bot gestoppt");
            } else {
                AppendLog(state, L">> Bot beendet (normal)");
            }
        }
        return 0;

    // -----------------------------------------------------------------------
    case WM_APP_STOP_KEY:
        if (state) HandleStop(hwnd, state);
        return 0;

    // -----------------------------------------------------------------------
    case WM_DESTROY:
        if (state) {
            state->stopFlag.store(true, std::memory_order_release);
            if (state->worker.joinable()) state->worker.join();

            if (state->keyboardHook) {
                UnhookWindowsHookEx(state->keyboardHook);
                state->keyboardHook = nullptr;
            }
            if (state->registeredHotkeyVk) {
                UnregisterHotKey(hwnd, ID_HOTKEY_TOGGLE);
                state->registeredHotkeyVk = 0;
            }
            if (state->hBoldFont) {
                DeleteObject(state->hBoldFont);
                state->hBoldFont = nullptr;
            }
        }
        PostQuitMessage(0);
        return 0;

    } // end switch

    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

// ---------------------------------------------------------------------------
// RunApplication
// ---------------------------------------------------------------------------

int RunApplication(HINSTANCE hInstance, int nCmdShow) {
    INITCOMMONCONTROLSEX icce{sizeof(icce), ICC_WIN95_CLASSES};
    InitCommonControlsEx(&icce);

    AppState state;
    g_state = &state;

    WNDCLASSEXW wc{};
    wc.cbSize        = sizeof(wc);
    wc.style         = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc   = WndProc;
    wc.hInstance     = hInstance;
    wc.hIcon         = LoadIconW(nullptr, IDI_APPLICATION);
    wc.hCursor       = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_3DFACE + 1);
    wc.lpszClassName = L"MinecraftAutoBot";
    wc.hIconSm       = wc.hIcon;

    if (!RegisterClassExW(&wc)) {
        MessageBoxW(nullptr,
                    L"Fensterklasse konnte nicht registriert werden.",
                    L"Fehler", MB_OK | MB_ICONERROR);
        g_state = nullptr;
        return 1;
    }

    RECT  rc{0, 0, kWinClientW, kWinClientH};
    DWORD style = WS_OVERLAPPEDWINDOW & ~WS_THICKFRAME & ~WS_MAXIMIZEBOX;
    AdjustWindowRect(&rc, style, FALSE);

    HWND hwnd = CreateWindowExW(
        0,
        L"MinecraftAutoBot",
        L"Minecraft Bedrock Auto Bot",
        style,
        CW_USEDEFAULT, CW_USEDEFAULT,
        rc.right  - rc.left,
        rc.bottom - rc.top,
        nullptr, nullptr,
        hInstance,
        &state);

    if (!hwnd) {
        MessageBoxW(nullptr,
                    L"Fenster konnte nicht erstellt werden.",
                    L"Fehler", MB_OK | MB_ICONERROR);
        g_state = nullptr;
        return 1;
    }

    ShowWindow(hwnd, nCmdShow);
    UpdateWindow(hwnd);

    MSG msg{};
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    g_state = nullptr;
    return static_cast<int>(msg.wParam);
}
