// screen_capture.cpp – GDI-based screen-capture implementation.
//
// Uses Win32 CreateDIBSection + BitBlt to copy screen pixels into a
// caller-owned byte buffer without any external dependencies.
//
// Windows-only; compiled with MSVC targeting Windows 10/11 x64.

#include "screen_capture.h"

#include <cstring>

// ---------------------------------------------------------------------------
// GdiScreenCapture
// ---------------------------------------------------------------------------

GdiScreenCapture::GdiScreenCapture() {
    m_sw = GetSystemMetrics(SM_CXSCREEN);
    m_sh = GetSystemMetrics(SM_CYSCREEN);
}

ScreenBitmap GdiScreenCapture::CaptureRegion(int x, int y, int w, int h) {
    ScreenBitmap result;
    if (w <= 0 || h <= 0) return result;

    HDC hdcScreen = GetDC(nullptr);
    if (!hdcScreen) return result;

    HDC hdcMem = CreateCompatibleDC(hdcScreen);
    if (!hdcMem) {
        ReleaseDC(nullptr, hdcScreen);
        return result;
    }

    // DIB section descriptor (32-bpp, bottom-up, BGRA).
    BITMAPINFO bmi{};
    bmi.bmiHeader.biSize        = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth       = w;
    bmi.bmiHeader.biHeight      = h;   // positive → bottom-up
    bmi.bmiHeader.biPlanes      = 1;
    bmi.bmiHeader.biBitCount    = 32;
    bmi.bmiHeader.biCompression = BI_RGB;

    void*   pBits = nullptr;
    HBITMAP hBmp  = CreateDIBSection(
        hdcScreen, &bmi, DIB_RGB_COLORS, &pBits, nullptr, 0);

    if (!hBmp || !pBits) {
        DeleteDC(hdcMem);
        ReleaseDC(nullptr, hdcScreen);
        return result;
    }

    HGDIOBJ hOld = SelectObject(hdcMem, hBmp);
    BOOL    ok   = BitBlt(hdcMem, 0, 0, w, h, hdcScreen, x, y, SRCCOPY);

    if (ok) {
        // GdiFlush ensures BitBlt has committed all pixels to the DIB.
        GdiFlush();
        const std::size_t byteCount =
            static_cast<std::size_t>(w) * static_cast<std::size_t>(h) * 4u;
        result.pixels.resize(byteCount);
        std::memcpy(result.pixels.data(), pBits, byteCount);
        result.width  = w;
        result.height = h;
        result.valid  = true;
    }

    SelectObject(hdcMem, hOld);
    DeleteObject(hBmp);
    DeleteDC(hdcMem);
    ReleaseDC(nullptr, hdcScreen);

    return result;
}
