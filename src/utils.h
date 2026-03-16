// utils.h – General-purpose utility declarations.
// Windows-only; compiled with MSVC targeting Windows 10/11 x64.

#pragma once

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>

#include <optional>
#include <string>

namespace Utils {

/// Parse a double from a wide string.
/// Returns std::nullopt if the string is empty, contains non-numeric characters,
/// or does not represent a complete number (trailing garbage is rejected).
std::optional<double> ParseDouble(const std::wstring& s);

/// Convert a UTF-8 encoded narrow string to a UTF-16 wide string.
std::wstring ToWide(const std::string& s);

/// Convert a UTF-16 wide string to a UTF-8 encoded narrow string.
std::string ToNarrow(const std::wstring& ws);

/// Return the current tick count in milliseconds (64-bit; no practical wrap-around).
ULONGLONG TickMs();

} // namespace Utils
