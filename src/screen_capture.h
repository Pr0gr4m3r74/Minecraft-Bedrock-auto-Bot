// screen_capture.h – Screen-capture interface and GDI implementation.
//
// IScreenCapture is the abstract interface; swap in an OpenCV VideoCapture
// or DXGI Desktop-Duplication implementation without touching any other code.
//
// Windows-only; compiled with MSVC targeting Windows 10/11 x64.

#pragma once

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>

#include <cstdint>
#include <vector>

// ---------------------------------------------------------------------------
// Captured-bitmap descriptor
// ---------------------------------------------------------------------------

/// Raw 32-bpp (BGRA, bottom-up) pixel data from a captured screen region.
struct ScreenBitmap {
    std::vector<uint8_t> pixels; ///< Pixel bytes: 4 bytes per pixel, BGRA order.
    int  width  = 0;             ///< Width in pixels.
    int  height = 0;             ///< Height in pixels.
    bool valid  = false;         ///< False when the capture failed.
};

// ---------------------------------------------------------------------------
// Abstract interface
// ---------------------------------------------------------------------------

/// Screen-capture interface.
///
/// The default implementation uses Win32 GDI BitBlt.
/// Replace with a DXGI Desktop-Duplication or OpenCV backend for better
/// performance and HDR/high-DPI support.
class IScreenCapture {
public:
    virtual ~IScreenCapture() = default;

    /// Capture a rectangle of the primary screen.
    /// @param x,y  Top-left corner in virtual screen coordinates.
    /// @param w,h  Width and height in pixels (must be > 0).
    /// @return     Bitmap with BGRA pixel data; valid == false on failure.
    virtual ScreenBitmap CaptureRegion(int x, int y, int w, int h) = 0;

    /// Width of the primary screen in pixels.
    virtual int ScreenWidth()  const = 0;

    /// Height of the primary screen in pixels.
    virtual int ScreenHeight() const = 0;
};

// ---------------------------------------------------------------------------
// GDI implementation (default – no additional dependencies required)
// ---------------------------------------------------------------------------

/// Screen capture using Win32 GDI BitBlt.
///
/// Suitable for windowed and exclusive full-screen applications when
/// desktop composition is active (Windows 8+).  Replace with a DXGI
/// Desktop-Duplication backend for exclusive-full-screen games.
class GdiScreenCapture final : public IScreenCapture {
public:
    GdiScreenCapture();

    ScreenBitmap CaptureRegion(int x, int y, int w, int h) override;

    int ScreenWidth()  const override { return m_sw; }
    int ScreenHeight() const override { return m_sh; }

private:
    int m_sw = 0;  ///< Cached primary-screen width.
    int m_sh = 0;  ///< Cached primary-screen height.
};
