// utils.cpp – General-purpose utility implementations.

#include "utils.h"

#include <stdexcept>

namespace Utils {

std::optional<double> ParseDouble(const std::wstring& s) {
    if (s.empty()) return std::nullopt;
    try {
        std::size_t idx = 0;
        double val = std::stod(s, &idx);
        // Reject strings with trailing non-numeric characters.
        if (idx != s.size()) return std::nullopt;
        return val;
    } catch (...) {
        return std::nullopt;
    }
}

std::wstring ToWide(const std::string& s) {
    if (s.empty()) return {};
    int len = MultiByteToWideChar(
        CP_UTF8, 0,
        s.data(), static_cast<int>(s.size()),
        nullptr, 0);
    if (len <= 0) return {};
    std::wstring result(static_cast<std::size_t>(len), L'\0');
    MultiByteToWideChar(
        CP_UTF8, 0,
        s.data(), static_cast<int>(s.size()),
        result.data(), len);
    return result;
}

std::string ToNarrow(const std::wstring& ws) {
    if (ws.empty()) return {};
    int len = WideCharToMultiByte(
        CP_UTF8, 0,
        ws.data(), static_cast<int>(ws.size()),
        nullptr, 0, nullptr, nullptr);
    if (len <= 0) return {};
    std::string result(static_cast<std::size_t>(len), '\0');
    WideCharToMultiByte(
        CP_UTF8, 0,
        ws.data(), static_cast<int>(ws.size()),
        result.data(), len, nullptr, nullptr);
    return result;
}

ULONGLONG TickMs() {
    return GetTickCount64();
}

} // namespace Utils
