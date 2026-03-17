// parser.h – Minecraft F3 debug-overlay coordinate parser declarations.
//
// CoordParser::Parse() scans multi-line OCR text for common patterns
// produced by Minecraft Java Edition and Bedrock Edition debug overlays
// and returns a PlayerPos if at least the XYZ block can be extracted.
//
// Windows-only; compiled with MSVC targeting Windows 10/11 x64.

#pragma once

#include <string>

// ---------------------------------------------------------------------------
// Player position / orientation
// ---------------------------------------------------------------------------

/// Position and facing direction parsed from the F3 debug overlay.
struct PlayerPos {
    double x     = 0.0;  ///< Block X coordinate.
    double y     = 0.0;  ///< Block Y coordinate (height).
    double z     = 0.0;  ///< Block Z coordinate.
    double yaw   = 0.0;  ///< Horizontal facing angle in degrees
                         ///  (Minecraft convention: 0=South, -90/270=East).
    double pitch = 0.0;  ///< Vertical facing angle in degrees (0=horizon).
    bool   valid = false;///< True only when x/y/z were successfully parsed.
};

// ---------------------------------------------------------------------------
// CoordParser namespace
// ---------------------------------------------------------------------------

namespace CoordParser {

/// Parse player coordinates from a multi-line F3 overlay text.
///
/// Recognises the following line formats (case-insensitive):
///   "XYZ: 123.456 / 64.000 / -78.912"       (Java Edition)
///   "Position: 123 64 -78"                   (Bedrock Edition)
///   "X: 123.456\nY: 64.000\nZ: -78.912"      (per-coordinate lines)
///   "Facing: north ..."                       (direction line)
///
/// @param text  Raw text string returned by the OCR engine.
/// @return      Parsed position; valid == false if nothing could be parsed.
PlayerPos Parse(const std::wstring& text);

/// Return true when `pos` is within `tolerance` blocks of `origin` in X and Z.
/// (Y / vertical axis is intentionally ignored for farm navigation purposes.)
bool NearOrigin(const PlayerPos& pos,
                const PlayerPos& origin,
                double tolerance = 2.0) noexcept;

/// Format a PlayerPos for display in the log window.
std::wstring FormatPos(const PlayerPos& pos);

} // namespace CoordParser
