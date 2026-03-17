// parser.cpp – Minecraft F3 debug-overlay coordinate parser implementation.
//
// Scans multi-line OCR text for the coordinate patterns that Minecraft
// Java Edition and Bedrock Edition write into the F3 debug screen.
//
// Windows-only; compiled with MSVC targeting Windows 10/11 x64.

#include "parser.h"

#include <algorithm>
#include <cwctype>
#include <optional>
#include <sstream>
#include <vector>

// ---------------------------------------------------------------------------
// Internal helpers
// ---------------------------------------------------------------------------

namespace {

/// Convert a wide string to lower-case (ASCII range only; sufficient for the
/// fixed English keywords we are searching for).
static std::wstring ToLower(std::wstring s) {
    for (wchar_t& c : s) {
        c = static_cast<wchar_t>(std::towlower(c));
    }
    return s;
}

/// Split `text` into individual lines.
static std::vector<std::wstring> SplitLines(const std::wstring& text) {
    std::vector<std::wstring> lines;
    std::wistringstream ss(text);
    std::wstring line;
    while (std::getline(ss, line)) {
        // Strip carriage return that may precede the newline.
        if (!line.empty() && line.back() == L'\r') line.pop_back();
        lines.push_back(line);
    }
    return lines;
}

/// Try to parse a double from the characters in `s` starting at position `pos`.
/// Advances `pos` past the parsed number.  Returns nullopt on failure.
static std::optional<double> ParseNum(const std::wstring& s, std::size_t& pos) {
    // Skip leading whitespace and an optional sign.
    while (pos < s.size() && std::iswspace(s[pos])) ++pos;
    if (pos >= s.size()) return std::nullopt;

    std::size_t end = pos;
    if (end < s.size() && (s[end] == L'-' || s[end] == L'+')) ++end;
    bool hasDot = false;
    while (end < s.size()) {
        if (s[end] == L'.' && !hasDot) { hasDot = true; ++end; }
        else if (std::iswdigit(s[end]))  { ++end; }
        else break;
    }
    if (end == pos || (end == pos + 1 && (s[pos] == L'-' || s[pos] == L'+'))) {
        return std::nullopt;
    }
    try {
        std::size_t eaten = 0;
        double v = std::stod(s.substr(pos, end - pos), &eaten);
        if (eaten == 0) return std::nullopt;
        pos = pos + eaten;
        return v;
    } catch (...) {
        return std::nullopt;
    }
}

/// Try to parse three space- or slash-separated doubles from `line`.
/// Returns a tuple {x,y,z} or nullopt.
static std::optional<std::tuple<double,double,double>>
ParseXYZ(const std::wstring& line) {
    std::size_t p = 0;
    auto x = ParseNum(line, p);
    if (!x) return std::nullopt;

    // Accept space, comma, or slash as separator.
    while (p < line.size() && (std::iswspace(line[p]) || line[p] == L'/' || line[p] == L',')) ++p;
    auto y = ParseNum(line, p);
    if (!y) return std::nullopt;

    while (p < line.size() && (std::iswspace(line[p]) || line[p] == L'/' || line[p] == L',')) ++p;
    auto z = ParseNum(line, p);
    if (!z) return std::nullopt;

    return std::make_tuple(*x, *y, *z);
}

/// Map a cardinal-direction word to a Minecraft yaw angle (degrees).
/// Minecraft yaw: 0 = South, 90 = West, 180 = North, 270 = East.
static std::optional<double> FacingToYaw(const std::wstring& word) {
    std::wstring w = ToLower(word);
    if (w == L"south")     return  0.0;
    if (w == L"west")      return  90.0;
    if (w == L"north")     return 180.0;
    if (w == L"east")      return 270.0;
    if (w == L"sw" || w == L"southwest") return 45.0;
    if (w == L"nw" || w == L"northwest") return 135.0;
    if (w == L"ne" || w == L"northeast") return 225.0;
    if (w == L"se" || w == L"southeast") return 315.0;
    return std::nullopt;
}

} // anonymous namespace

// ---------------------------------------------------------------------------
// CoordParser::Parse
// ---------------------------------------------------------------------------

PlayerPos CoordParser::Parse(const std::wstring& text) {
    PlayerPos result;

    // Work on a lower-cased copy for keyword matching; keep original for
    // numeric parsing so locale decimal-separator issues don't arise.
    auto lines    = SplitLines(text);

    std::optional<double> ox, oy, oz;
    std::optional<double> oyaw, opitch;

    for (const auto& rawLine : lines) {
        std::wstring low = ToLower(rawLine);

        // ---- XYZ: x / y / z   (Java Edition) ----
        auto xyzPos = low.find(L"xyz:");
        if (xyzPos != std::wstring::npos) {
            std::size_t p = xyzPos + 4;
            auto triple = ParseXYZ(rawLine.substr(p));
            if (triple) {
                ox = std::get<0>(*triple);
                oy = std::get<1>(*triple);
                oz = std::get<2>(*triple);
                continue;
            }
        }

        // ---- Position: x y z   (Bedrock Edition) ----
        auto posPos = low.find(L"position:");
        if (posPos != std::wstring::npos) {
            std::size_t p = posPos + 9;
            auto triple = ParseXYZ(rawLine.substr(p));
            if (triple) {
                ox = std::get<0>(*triple);
                oy = std::get<1>(*triple);
                oz = std::get<2>(*triple);
                continue;
            }
        }

        // ---- X: value  /  Y: value  /  Z: value  (per-line format) ----
        auto tryAxis = [&](const wchar_t* key, std::optional<double>& out) {
            auto kp = low.find(key);
            if (kp != std::wstring::npos) {
                std::size_t p = kp + wcslen(key);
                auto v = ParseNum(rawLine, p);
                if (v) out = v;
            }
        };
        tryAxis(L"x:", ox);
        tryAxis(L"y:", oy);
        tryAxis(L"z:", oz);

        // ---- Facing: north / south / east / west ----
        auto facPos = low.find(L"facing:");
        if (facPos != std::wstring::npos) {
            // Extract the first word after "facing:"
            std::size_t p = facPos + 7;
            while (p < rawLine.size() && std::iswspace(rawLine[p])) ++p;
            std::size_t end = p;
            while (end < rawLine.size() && std::iswalpha(rawLine[end])) ++end;
            if (end > p) {
                auto yaw = FacingToYaw(rawLine.substr(p, end - p));
                if (yaw) oyaw = yaw;
            }
        }

        // ---- f: pitch / yaw   (compact Java Edition line) ----
        // Format: "f: 2 (Facing west)"
        auto fPos = low.find(L"f:");
        if (fPos != std::wstring::npos) {
            // Try to find a word after 'facing' within the same line
            auto fw = low.find(L"facing", fPos);
            if (fw != std::wstring::npos) {
                std::size_t p = fw + 6;
                while (p < rawLine.size() && std::iswspace(rawLine[p])) ++p;
                std::size_t end = p;
                while (end < rawLine.size() && std::iswalpha(rawLine[end])) ++end;
                if (end > p) {
                    auto yaw = FacingToYaw(rawLine.substr(p, end - p));
                    if (yaw) oyaw = yaw;
                }
            }
        }
    }

    if (ox && oy && oz) {
        result.x     = *ox;
        result.y     = *oy;
        result.z     = *oz;
        result.yaw   = oyaw   ? *oyaw   : 0.0;
        result.pitch = opitch ? *opitch : 0.0;
        result.valid = true;
    }

    return result;
}

// ---------------------------------------------------------------------------
// CoordParser::NearOrigin
// ---------------------------------------------------------------------------

bool CoordParser::NearOrigin(const PlayerPos& pos,
                             const PlayerPos& origin,
                             double tolerance) noexcept {
    if (!pos.valid || !origin.valid) return false;
    double dx = pos.x - origin.x;
    double dz = pos.z - origin.z;
    return (dx * dx + dz * dz) <= (tolerance * tolerance);
}

// ---------------------------------------------------------------------------
// CoordParser::FormatPos
// ---------------------------------------------------------------------------

std::wstring CoordParser::FormatPos(const PlayerPos& pos) {
    if (!pos.valid) return L"(ungültig)";
    wchar_t buf[128];
    swprintf_s(buf,
        L"X=%.2f Y=%.2f Z=%.2f Yaw=%.1f°",
        pos.x, pos.y, pos.z, pos.yaw);
    return buf;
}
