// main.cpp – Application entry point.
//
// wWinMain is the Unicode entry point for Win32 GUI applications.
// It delegates immediately to RunApplication() in gui.cpp.
//
// Compiler: MSVC  |  Platform: Windows 10/11 x64  |  Standard: C++17

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>

#include "gui.h"

// Link against comctl32 v6 to get visual styles on Windows 10/11.
#pragma comment(lib, "comctl32.lib")
// Request comctl32 v6 via the side-by-side manifest.
#pragma comment(linker,                                              \
    "/manifestdependency:\""                                         \
    "type='win32' "                                                  \
    "name='Microsoft.Windows.Common-Controls' "                      \
    "version='6.0.0.0' "                                             \
    "processorArchitecture='*' "                                     \
    "publicKeyToken='6595b64144ccf1df' "                             \
    "language='*'\"")

int WINAPI wWinMain(
    _In_     HINSTANCE hInstance,
    _In_opt_ HINSTANCE /*hPrevInstance*/,
    _In_     LPWSTR    /*lpCmdLine*/,
    _In_     int        nCmdShow)
{
    return RunApplication(hInstance, nCmdShow);
}
