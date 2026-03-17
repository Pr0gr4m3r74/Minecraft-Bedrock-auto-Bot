// gui.cpp – Win32 main-window creation and message handling.
//
// Layout is built entirely from CreateWindowExW calls (no resource file needed).
// All strings are UTF-16; text shown to the user is in German to match the
// original Python implementation.

#include "gui.h"
#include "bot.h"
#include "utils.h"

#include <commctrl.h>
#include <optional>
#include <string>
#include <memory>

// ---------------------------------------------------------------------------
// Window layout constants (logical pixels at 96 DPI; scaled by the OS on
// higher-DPI displays when the DPI-awareness manifest entry is present).
// ---------------------------------------------------------------------------
static constexpr int kWinClientW = 470;  ///< Target client area width.
static constexpr int kWinClientH = 440;  ///< Target client area height.
static constexpr int kMx         = 12;   ///< Horizontal margin.
static constexpr int kMy         = 12;   ///< Vertical margin.
static constexpr int kRowH       = 24;   ///< Height of a label/edit row.
static constexpr int kRowGap     = 6;    ///< Gap between rows.
static constexpr int kLabelW     = 230;  ///< Width of the label column.
static constexpr int kEditW      = 90;   ///< Width of the edit-box column.
static constexpr int kBtnW       = 130;  ///< Standard button width.
static constexpr int kBtnH       = 28;   ///< Standard button height.
static constexpr int kBtnGap     = 8;    ///< Gap between adjacent buttons.

// ---------------------------------------------------------------------------
// Global hook-callback state
//
// WH_KEYBOARD_LL callbacks receive no user-defined parameter, so we use a
// module-level pointer.  Only one BotApp instance runs at a time.
// ---------------------------------------------------------------------------
static AppState* g_state = nullptr;

// ---------------------------------------------------------------------------
// Low-level keyboard hook
//
// Installed when the user starts the bot (including during the countdown) and
// uninstalled when the run ends.  Any key press triggers a stop unless the
// ignoreStopKeys flag is set (set while movement keys are held to prevent
// injected key-down events from being mistaken for user input).
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
    // Always forward to the next hook in the chain so other applications
    // are not affected by the presence of this hook.
    return CallNextHookEx(nullptr, nCode, wParam, lParam);
}

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

/// Read the text of a child edit control into a std::wstring.
static std::wstring GetEditText(HWND hEdit) {
    int len = GetWindowTextLengthW(hEdit);
    if (len <= 0) return {};
    std::wstring s(static_cast<std::size_t>(len), L'\0');
    GetWindowTextW(hEdit, s.data(), len + 1);
    return s;
}

/// Enable or disable all input controls depending on whether a run is active.
static void ToggleControls(AppState* state, bool running) {
    BOOL inputEnabled = running ? FALSE : TRUE;
    EnableWindow(state->hEditCountdown, inputEnabled);
    EnableWindow(state->hEditBreakTime, inputEnabled);
    EnableWindow(state->hEditStepTime,  inputEnabled);
    EnableWindow(state->hEditReplant,   inputEnabled);
    EnableWindow(state->hBtnStart,      inputEnabled);
    EnableWindow(state->hBtnInstall,    inputEnabled);
    EnableWindow(state->hBtnUninstall,  inputEnabled);
    // Stop button is enabled while running and disabled while idle.
    EnableWindow(state->hBtnStop, running ? TRUE : FALSE);
}

/// Thread-safe status update: post a heap-allocated string to the GUI thread.
/// The GUI thread deletes the string after displaying it (see WM_APP_STATUS).
static void PostStatus(AppState* state, const wchar_t* text) {
    auto* msg = new std::wstring(text);
    if (!PostMessage(state->hwnd, WM_APP_STATUS, 0,
                     reinterpret_cast<LPARAM>(msg))) {
        // PostMessage failed (window already destroyed); avoid a leak.
        delete msg;
    }
}

// ---------------------------------------------------------------------------
// Action handlers
// ---------------------------------------------------------------------------

/// Uninstall the keyboard hook, re-enable controls, and optionally reset
/// the status label.  Must be called on the GUI thread.
static void CleanupAfterRun(HWND hwnd, AppState* state, bool stopped) {
    state->stopFlag.store(true, std::memory_order_release);

    if (state->keyboardHook) {
        UnhookWindowsHookEx(state->keyboardHook);
        state->keyboardHook = nullptr;
    }

    KillTimer(hwnd, IDT_COUNTDOWN);
    ToggleControls(state, false);

    // Only reset the status if the run was interrupted before completing.
    // A completed run has already set the label to "Fertig ...".
    if (stopped) {
        wchar_t buf[128]{};
        GetWindowTextW(state->hStaticStatus, buf, _countof(buf));
        // "Fertig" is 6 characters; keep the label if the run finished normally.
        if (wcsncmp(buf, L"Fertig", 6) != 0) {
            SetWindowTextW(state->hStaticStatus, L"Bereit");
        }
    }
}

/// Respond to a stop request (Stop button, Escape key, or keyboard hook).
static void HandleStop(HWND hwnd, AppState* state) {
    SetWindowTextW(state->hStaticStatus, L"Stop angefordert");
    state->stopFlag.store(true, std::memory_order_release);
    KillTimer(hwnd, IDT_COUNTDOWN);
}

/// Start the bot worker thread after the countdown has elapsed.
static void LaunchWorker(AppState* state) {
    // Join the previous worker if it is still joinable (e.g. very fast run).
    if (state->worker.joinable()) {
        state->worker.join();
    }

    BotConfig cfg{state->botBreakTime, state->botStepTime, state->botReplantPause};

    state->worker = std::thread([state, cfg]() {
        Bot bot(
            state->stopFlag,
            state->ignoreStopKeys,
            [state](const wchar_t* txt) { PostStatus(state, txt); });

        bot.Run(cfg);

        // Capture whether the run was stopped *before* posting the done message
        // (CleanupAfterRun will set stopFlag=true unconditionally).
        bool stopped = state->stopFlag.load(std::memory_order_acquire);
        PostMessage(state->hwnd, WM_APP_RUN_DONE,
                    stopped ? 1u : 0u, 0);
    });
}

/// Validate the user's inputs, confirm with a dialog, then start the countdown.
static void HandleStart(HWND hwnd, AppState* state) {
    // Parse each timing field; reject the start if any field is invalid.
    auto parseField = [&](HWND hEdit, double minVal) -> std::optional<double> {
        auto v = Utils::ParseDouble(GetEditText(hEdit));
        if (!v) return std::nullopt;
        return std::max(minVal, *v);
    };

    auto optCountdown = parseField(state->hEditCountdown, 0.0);
    auto optBreak     = parseField(state->hEditBreakTime, 0.1);
    auto optStep      = parseField(state->hEditStepTime,  0.05);
    auto optReplant   = parseField(state->hEditReplant,   0.0);

    if (!optCountdown || !optBreak || !optStep || !optReplant) {
        MessageBoxW(hwnd, L"Bitte gültige Zahlen eingeben.",
                    L"Fehler", MB_OK | MB_ICONERROR);
        return;
    }

    // Confirm that Minecraft is in the foreground.
    if (MessageBoxW(hwnd,
            L"Der Bot übernimmt Tastatur und Maus.\n"
            L"Stelle sicher, dass Minecraft im Vordergrund ist.",
            L"Warnung", MB_OKCANCEL | MB_ICONWARNING) != IDOK) {
        return;
    }

    // Store validated bot config.
    state->botBreakTime    = *optBreak;
    state->botStepTime     = *optStep;
    state->botReplantPause = *optReplant;

    // Disable input controls for the duration of the run.
    ToggleControls(state, true);
    state->stopFlag.store(false, std::memory_order_release);

    // Install the keyboard hook now so any key press (even during the
    // countdown) stops the bot – mirroring the Python implementation.
    if (state->keyboardHook) {
        UnhookWindowsHookEx(state->keyboardHook);
    }
    state->keyboardHook = SetWindowsHookExW(
        WH_KEYBOARD_LL, LowLevelKeyboardProc, nullptr, 0);

    // Start the countdown Win32 timer (100 ms tick).
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

        // Retrieve the default system UI font for consistent appearance.
        HFONT hFont = reinterpret_cast<HFONT>(
            GetStockObject(DEFAULT_GUI_FONT));

        // ---- Helper lambdas to reduce boilerplate ----

        auto MakeLabel = [&](int x, int y, int w, int h,
                             const wchar_t* text) -> HWND {
            HWND h_ = CreateWindowExW(
                0, L"STATIC", text,
                WS_CHILD | WS_VISIBLE | SS_LEFT,
                x, y, w, h, hwnd, nullptr, cs->hInstance, nullptr);
            SendMessageW(h_, WM_SETFONT,
                         reinterpret_cast<WPARAM>(hFont), TRUE);
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
            SendMessageW(h_, WM_SETFONT,
                         reinterpret_cast<WPARAM>(hFont), TRUE);
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
            SendMessageW(h_, WM_SETFONT,
                         reinterpret_cast<WPARAM>(hFont), TRUE);
            return h_;
        };

        // ---- Build the control layout ----
        int y = kMy;
        const int editX = kMx + kLabelW + kBtnGap;

        // Row 0 – Countdown
        MakeLabel(kMx, y + 3, kLabelW, kRowH,
                  L"Countdown (Sekunden)");
        state->hEditCountdown = MakeEdit(editX, y, kEditW, kRowH,
                                         L"3", IDC_EDIT_COUNTDOWN);
        y += kRowH + kRowGap;

        // Row 1 – Break-hold duration
        MakeLabel(kMx, y + 3, kLabelW, kRowH,
                  L"Abbau-Haltedauer (Sekunden)");
        state->hEditBreakTime = MakeEdit(editX, y, kEditW, kRowH,
                                         L"0.6", IDC_EDIT_BREAKTIME);
        y += kRowH + kRowGap;

        // Row 2 – Step duration
        MakeLabel(kMx, y + 3, kLabelW, kRowH,
                  L"Schritt-Dauer pro Block (Sekunden)");
        state->hEditStepTime = MakeEdit(editX, y, kEditW, kRowH,
                                        L"0.25", IDC_EDIT_STEPTIME);
        y += kRowH + kRowGap;

        // Row 3 – Replant pause
        MakeLabel(kMx, y + 3, kLabelW, kRowH,
                  L"Pause nach Pflanzen (Sekunden)");
        state->hEditReplant = MakeEdit(editX, y, kEditW, kRowH,
                                       L"0.1", IDC_EDIT_REPLANT);
        y += kRowH + kRowGap + 8;

        // Row 4 – Start / Stop buttons
        state->hBtnStart = MakeButton(kMx, y, kBtnW, kBtnH,
                                      L"Start", IDC_BTN_START);
        state->hBtnStop  = MakeButton(kMx + kBtnW + kBtnGap, y,
                                      160, kBtnH,
                                      L"Stopp (beliebige Taste)",
                                      IDC_BTN_STOP);
        EnableWindow(state->hBtnStop, FALSE);
        y += kBtnH + kRowGap + 8;

        // Row 5 – Status
        MakeLabel(kMx, y + 2, 52, kRowH, L"Status:");
        state->hStaticStatus = MakeLabel(
            kMx + 56, y + 2, kWinClientW - kMx - 56, kRowH, L"Bereit");
        y += kRowH + kRowGap + 4;

        // Row 6 – Instructions heading (bold)
        HWND hHeading = CreateWindowExW(
            0, L"STATIC", L"Anleitung",
            WS_CHILD | WS_VISIBLE | SS_LEFT,
            kMx, y, kWinClientW - kMx * 2, kRowH,
            hwnd, nullptr, cs->hInstance, nullptr);
        {
            // Create a bold variant of the default UI font; store the handle
            // in AppState so it can be cleaned up in WM_DESTROY.
            LOGFONTW lf{};
            GetObjectW(hFont, sizeof(lf), &lf);
            lf.lfWeight = FW_BOLD;
            state->hBoldFont = CreateFontIndirectW(&lf);
            SendMessageW(hHeading, WM_SETFONT,
                         reinterpret_cast<WPARAM>(state->hBoldFont), TRUE);
        }
        y += kRowH + 2;

        // Row 7 – Instructions text (multi-line static)
        MakeLabel(kMx, y, kWinClientW - kMx * 2, 110,
            L"1) Minecraft starten und das Fenster im Vordergrund lassen.\r\n"
            L"2) Auf Position (0,1) stellen, Blick nach Osten, "
               L"Kartoffeln in der Hand.\r\n"
            L"3) \"Start\" drücken – nach dem Countdown läuft der Bot automatisch.\r\n"
            L"4) Jede beliebige Taste (oder \"Stopp\") beendet den Bot sofort.\r\n"
            L"5) Zum Deinstallieren: diese EXE-Datei löschen.");
        y += 116;

        // Row 8 – Install / Uninstall buttons
        state->hBtnInstall = MakeButton(kMx, y, kBtnW, kBtnH,
                                        L"Installieren",   IDC_BTN_INSTALL);
        state->hBtnUninstall = MakeButton(kMx + kBtnW + kBtnGap, y,
                                          kBtnW, kBtnH,
                                          L"Deinstallieren", IDC_BTN_UNINSTALL);

        // Show the welcome dialog after the window becomes visible.
        PostMessage(hwnd, WM_APP_WELCOME, 0, 0);
        return 0;
    }

    // -----------------------------------------------------------------------
    case WM_APP_WELCOME:
        MessageBoxW(hwnd,
            L"Minecraft Bedrock Auto Bot\n\n"
            L"1) Starte Minecraft und halte das Fenster im Vordergrund.\n"
            L"2) Stelle dich auf Position (0,1), schaue nach Osten "
               L"und halte Kartoffeln in der Hand.\n"
            L"3) Drücke \"Start\" – nach dem Countdown läuft der Bot, "
               L"bis du eine Taste drückst oder \"Stopp\" wählst.\n"
            L"4) Zum Deinstallieren: diese EXE-Datei und den Ordner löschen.",
            L"Anleitung", MB_OK | MB_ICONINFORMATION);
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
                L"Diese Anwendung ist eigenständig und benötigt "
                L"keine zusätzliche Installation.",
                L"Installation", MB_OK | MB_ICONINFORMATION);
            break;
        case IDC_BTN_UNINSTALL:
            if (MessageBoxW(hwnd,
                    L"Zum Deinstallieren löschen Sie einfach diese "
                    L"EXE-Datei.\nFortfahren?",
                    L"Deinstallieren",
                    MB_YESNO | MB_ICONQUESTION) == IDYES) {
                MessageBoxW(hwnd,
                    L"Löschen Sie diese Datei, um die Anwendung "
                    L"vollständig zu entfernen.",
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
                // Countdown elapsed or was cancelled.
                KillTimer(hwnd, IDT_COUNTDOWN);
                if (!state->stopFlag.load(std::memory_order_acquire)) {
                    SetWindowTextW(state->hStaticStatus, L"Starte...");
                    LaunchWorker(state);
                } else {
                    CleanupAfterRun(hwnd, state, true);
                }
            } else {
                // Update the remaining-time display.
                double remaining =
                    static_cast<double>(state->countdownEndTick - now) /
                    1000.0;
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
    case WM_APP_RUN_DONE:
        if (state) {
            bool stopped = (wParam != 0);

            // Uninstall the keyboard hook.
            if (state->keyboardHook) {
                UnhookWindowsHookEx(state->keyboardHook);
                state->keyboardHook = nullptr;
            }

            ToggleControls(state, false);

            // Reset status to "Bereit" only if the run was interrupted;
            // a completed run already set the label to "Fertig ...".
            if (stopped) {
                wchar_t buf[128]{};
                GetWindowTextW(state->hStaticStatus, buf, _countof(buf));
                if (wcsncmp(buf, L"Fertig", 6) != 0) {
                    SetWindowTextW(state->hStaticStatus, L"Bereit");
                }
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
            // Signal the worker to stop and wait for it to exit cleanly.
            // The bot polls the stop flag every 10 ms, so this join is brief.
            state->stopFlag.store(true, std::memory_order_release);
            if (state->worker.joinable()) {
                state->worker.join();
            }
            if (state->keyboardHook) {
                UnhookWindowsHookEx(state->keyboardHook);
                state->keyboardHook = nullptr;
            }
            // Release the GDI font object created for the bold heading.
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
    // Initialise common controls (required for themed button/edit rendering).
    INITCOMMONCONTROLSEX icce{sizeof(icce), ICC_WIN95_CLASSES};
    InitCommonControlsEx(&icce);

    // Allocate application state on the stack; it lives for the entire
    // duration of the message loop.
    AppState state;
    g_state = &state;

    // Register the window class.
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

    // Calculate the full window size so the *client area* matches the layout.
    RECT rc{0, 0, kWinClientW, kWinClientH};
    DWORD style = (WS_OVERLAPPEDWINDOW &
                   ~WS_THICKFRAME & ~WS_MAXIMIZEBOX);
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
        &state);   // passed as lpCreateParams → available in WM_CREATE

    if (!hwnd) {
        MessageBoxW(nullptr,
                    L"Fenster konnte nicht erstellt werden.",
                    L"Fehler", MB_OK | MB_ICONERROR);
        g_state = nullptr;
        return 1;
    }

    ShowWindow(hwnd, nCmdShow);
    UpdateWindow(hwnd);

    // Standard Win32 message loop.
    MSG msg{};
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    g_state = nullptr;
    return static_cast<int>(msg.wParam);
}
