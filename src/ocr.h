// ocr.h – OCR interface and null-implementation placeholder.
//
// IOcr is the abstract interface; replace NullOcr with a Tesseract,
// Windows.Media.Ocr, or any other backend without touching other code.
//
// When NullOcr is active the bot falls back to a pure timing-based mode
// and logs a warning on every coordinate-check cycle.
//
// Windows-only; compiled with MSVC targeting Windows 10/11 x64.

#pragma once

#include "screen_capture.h"

#include <string>

// ---------------------------------------------------------------------------
// OCR result
// ---------------------------------------------------------------------------

/// Text and confidence returned by an OCR engine.
struct OcrResult {
    std::wstring text;           ///< Raw recognised text (may be empty).
    float        confidence = 0.f; ///< 0.0 = no data, 1.0 = fully confident.
    bool         available  = false; ///< False when no OCR engine is configured.
};

// ---------------------------------------------------------------------------
// Abstract interface
// ---------------------------------------------------------------------------

/// Abstract OCR interface.
///
/// The contract:
///   - Extract() is called from the bot worker thread with a ScreenBitmap
///     that contains the F3 debug-overlay region.
///   - The implementation returns the recognised text and a confidence score.
///   - If confidence < a caller-defined threshold the bot enters ErrorRecovery.
///   - If available == false the bot treats the call as "OCR not configured"
///     and falls back to timing-only navigation (with a logged warning).
class IOcr {
public:
    virtual ~IOcr() = default;

    /// Run OCR on `bmp` and return the recognised text.
    /// Thread-safe: may be called from any thread.
    virtual OcrResult Extract(const ScreenBitmap& bmp) = 0;
};

// ---------------------------------------------------------------------------
// Null implementation – placeholder until a real engine is integrated
// ---------------------------------------------------------------------------

/// Stub OCR that always reports "unavailable".
///
/// Replace this class with a concrete engine (e.g. Tesseract via
/// libtesseract, or the WinRT Windows.Media.Ocr API) to enable
/// coordinate-based position validation.
///
/// Example replacement header:
/// @code
/// #include "tesseract/baseapi.h"
/// class TesseractOcr final : public IOcr { ... };
/// @endcode
class NullOcr final : public IOcr {
public:
    /// Always returns an unavailable, zero-confidence result.
    OcrResult Extract(const ScreenBitmap& /*bmp*/) override {
        return {};  // available=false, confidence=0
    }
};
