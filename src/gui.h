// gui.h – Win32 main-window declarations and shared application state.
//
// Windows-only; compiled with MSVC targeting Windows 10/11 x64.

#pragma once

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>

#include <atomic>
#include <string>
#include <thread>

// ---------------------------------------------------------------------------
// Custom window messages (WM_APP range to avoid conflicts with system messages)
// ---------------------------------------------------------------------------

/// Posted by the worker thread to update the status label.
/// LPARAM carries a heap-allocated std::wstring* that the GUI thread must delete.
#define WM_APP_STATUS    (WM_APP + 1)

/// Posted by the worker thread when the bot run finishes.
/// WPARAM = 1 if stopped by the user, 0 if the run completed normally.
#define WM_APP_RUN_DONE  (WM_APP + 2)

/// Posted by the low-level keyboard hook when a key is pressed during a run.
/// The window procedure calls HandleStop in response.
#define WM_APP_STOP_KEY  (WM_APP + 3)

/// Posted by WM_CREATE to show the welcome dialog after the window is visible.
#define WM_APP_WELCOME   (WM_APP + 4)

// ---------------------------------------------------------------------------
// Control identifiers
// ---------------------------------------------------------------------------
#define IDC_EDIT_COUNTDOWN  101
#define IDC_EDIT_BREAKTIME  102
#define IDC_EDIT_STEPTIME   103
#define IDC_EDIT_REPLANT    104
#define IDC_BTN_START       105
#define IDC_BTN_STOP        106
#define IDC_BTN_INSTALL     107
#define IDC_BTN_UNINSTALL   108

// ---------------------------------------------------------------------------
// Timer identifier
// ---------------------------------------------------------------------------
#define IDT_COUNTDOWN  1

// ---------------------------------------------------------------------------
// Application state (owned by RunApplication; pointer stored in GWLP_USERDATA)
// ---------------------------------------------------------------------------

/// All mutable state for one application instance.
/// The struct lives on the stack inside RunApplication() and is therefore
/// valid for the entire lifetime of the message loop.
struct AppState {
    // Window handles populated during WM_CREATE.
    HWND hwnd           = nullptr;
    HWND hEditCountdown = nullptr;
    HWND hEditBreakTime = nullptr;
    HWND hEditStepTime  = nullptr;
    HWND hEditReplant   = nullptr;
    HWND hBtnStart      = nullptr;
    HWND hBtnStop       = nullptr;
    HWND hBtnInstall    = nullptr;
    HWND hBtnUninstall  = nullptr;
    HWND hStaticStatus  = nullptr;

    // Synchronisation between the GUI thread and the bot worker thread.
    std::atomic<bool> stopFlag{false};
    /// Set while a movement key is physically held so the keyboard hook does
    /// not misinterpret the injected key event as a user stop request.
    std::atomic<bool> ignoreStopKeys{false};
    std::thread       worker;

    // Handle of the installed WH_KEYBOARD_LL hook (nullptr when inactive).
    HHOOK keyboardHook = nullptr;

    // Bold font created for the "Anleitung" heading; deleted in WM_DESTROY.
    HFONT hBoldFont = nullptr;

    // Countdown timer: absolute tick value (GetTickCount64) at which the
    // countdown expires and the bot should start.
    ULONGLONG countdownEndTick = 0;

    // Bot timing parameters captured when the user presses Start.
    double botBreakTime    = 0.6;
    double botStepTime     = 0.25;
    double botReplantPause = 0.1;
};

// ---------------------------------------------------------------------------
// Application entry point
// ---------------------------------------------------------------------------

/// Register the window class, create the main window, and run the message loop.
/// Returns the exit code from PostQuitMessage.
int RunApplication(HINSTANCE hInstance, int nCmdShow);
