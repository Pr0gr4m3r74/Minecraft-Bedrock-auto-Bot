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
// Custom window messages (WM_APP range)
// ---------------------------------------------------------------------------

/// Posted by the worker thread to update the status label.
/// LPARAM carries a heap-allocated std::wstring* that the GUI thread must delete.
#define WM_APP_STATUS    (WM_APP + 1)

/// Posted by the worker thread when the bot run finishes.
/// WPARAM = 1 if stopped by the user, 0 if the run completed normally.
#define WM_APP_RUN_DONE  (WM_APP + 2)

/// Posted by the low-level keyboard hook when a key is pressed during a run.
#define WM_APP_STOP_KEY  (WM_APP + 3)

/// Posted by WM_CREATE to show the welcome dialog after the window is visible.
#define WM_APP_WELCOME   (WM_APP + 4)

/// Posted by the worker thread to append a line to the log list-box.
/// LPARAM carries a heap-allocated std::wstring* that the GUI thread must delete.
#define WM_APP_LOG       (WM_APP + 5)

// ---------------------------------------------------------------------------
// Control identifiers
// ---------------------------------------------------------------------------
#define IDC_EDIT_FARMWIDTH      101
#define IDC_EDIT_FARMHEIGHT     102
#define IDC_EDIT_BLOCKDELAY     103
#define IDC_EDIT_BREAKDELAY     104
#define IDC_EDIT_REPLANT        105
#define IDC_EDIT_WAITGROWTH     106
#define IDC_EDIT_RESETINTERVAL  107
#define IDC_EDIT_HOTKEY         108
#define IDC_EDIT_COUNTDOWN      109
#define IDC_BTN_START           110
#define IDC_BTN_STOP            111
#define IDC_BTN_INSTALL         112
#define IDC_BTN_UNINSTALL       113
#define IDC_LIST_LOG            114

// ---------------------------------------------------------------------------
// Timer identifier
// ---------------------------------------------------------------------------
#define IDT_COUNTDOWN  1

// ---------------------------------------------------------------------------
// Global hotkey identifier (used with RegisterHotKey / UnregisterHotKey)
// ---------------------------------------------------------------------------
#define ID_HOTKEY_TOGGLE  1

// ---------------------------------------------------------------------------
// Application state
// ---------------------------------------------------------------------------

/// All mutable state for one application instance.
struct AppState {
    // Window handles populated during WM_CREATE.
    HWND hwnd               = nullptr;
    HWND hEditFarmWidth     = nullptr;
    HWND hEditFarmHeight    = nullptr;
    HWND hEditBlockDelay    = nullptr;
    HWND hEditBreakDelay    = nullptr;
    HWND hEditReplant       = nullptr;
    HWND hEditWaitGrowth    = nullptr;
    HWND hEditResetInterval = nullptr;
    HWND hEditHotkey        = nullptr;
    HWND hEditCountdown     = nullptr;
    HWND hBtnStart          = nullptr;
    HWND hBtnStop           = nullptr;
    HWND hBtnInstall        = nullptr;
    HWND hBtnUninstall      = nullptr;
    HWND hStaticStatus      = nullptr;
    HWND hListLog           = nullptr;  ///< Log list-box (LBS_NOINTEGRALHEIGHT).

    // Synchronisation between GUI thread and the bot worker thread.
    std::atomic<bool> stopFlag{false};
    std::atomic<bool> ignoreStopKeys{false};
    std::thread       worker;

    // Low-level keyboard hook handle (nullptr when inactive).
    HHOOK keyboardHook = nullptr;

    // Bold font for headings; deleted in WM_DESTROY.
    HFONT hBoldFont = nullptr;

    // Countdown timer: absolute tick value (GetTickCount64) when bot launches.
    ULONGLONG countdownEndTick = 0;

    // Currently registered hotkey virtual-key code (0 = none).
    UINT registeredHotkeyVk = 0;

    // Bot config captured when the user presses Start.
    int    botFarmWidth      = 9;
    int    botFarmHeight     = 9;
    double botBlockDelay     = 0.25;
    double botBreakDelay     = 0.60;
    double botReplantPause   = 0.10;
    double botWaitForGrowth  = 300.0;
    int    botResetInterval  = 9;
    UINT   botStartStopVk    = VK_F6;
};

// ---------------------------------------------------------------------------
// Application entry point
// ---------------------------------------------------------------------------

/// Register the window class, create the main window, and run the message loop.
int RunApplication(HINSTANCE hInstance, int nCmdShow);
