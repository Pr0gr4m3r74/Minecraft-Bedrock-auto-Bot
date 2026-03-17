cmake_minimum_required(VERSION 3.20)

project(MinecraftBedrockAutoBot
    VERSION 1.0.0
    DESCRIPTION "Minecraft Bedrock automated farm bot (Win32/C++17)"
    LANGUAGES CXX)

# ---------------------------------------------------------------------------
# Platform guard – this project is Windows-only.
# ---------------------------------------------------------------------------
if(NOT WIN32)
    message(FATAL_ERROR
        "MinecraftBedrockAutoBot only supports Windows 10/11. "
        "Build with MSVC on a Windows host.")
endif()

# ---------------------------------------------------------------------------
# C++ standard
# ---------------------------------------------------------------------------
set(CMAKE_CXX_STANDARD          17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS        OFF)

# ---------------------------------------------------------------------------
# Windows version targeting (0x0A00 = Windows 10)
# ---------------------------------------------------------------------------
add_compile_definitions(
    _WIN32_WINNT=0x0A00
    WINVER=0x0A00
    WIN32_LEAN_AND_MEAN
    UNICODE
    _UNICODE
    NOMINMAX          # Prevent <Windows.h> from defining min/max macros.
)

# ---------------------------------------------------------------------------
# Source files
# ---------------------------------------------------------------------------
set(SOURCES
    src/main.cpp
    src/gui.cpp
    src/bot.cpp
    src/utils.cpp
    src/parser.cpp
    src/screen_capture.cpp
)

# WIN32 tells CMake to use wWinMain (GUI subsystem, no console window).
add_executable(MinecraftBedrockAutoBot WIN32 ${SOURCES})

target_include_directories(MinecraftBedrockAutoBot PRIVATE src)

# ---------------------------------------------------------------------------
# Required Windows libraries
# ---------------------------------------------------------------------------
target_link_libraries(MinecraftBedrockAutoBot PRIVATE
    user32      # CreateWindowExW, SendInput, SetWindowsHookExW, etc.
    gdi32       # GetStockObject, CreateFontIndirectW
    comctl32    # InitCommonControlsEx (visual styles)
    shell32     # (reserved for future shell integration)
)

# ---------------------------------------------------------------------------
# MSVC-specific compiler options
# ---------------------------------------------------------------------------
if(MSVC)
    target_compile_options(MinecraftBedrockAutoBot PRIVATE
        /W4           # Enable warning level 4.
        /WX           # Treat warnings as errors.
        /utf-8        # Interpret source files as UTF-8.
        /permissive-  # Enforce strict C++ conformance.
        /Zc:__cplusplus  # Report correct __cplusplus value.
    )

    # Place the binary in a predictable location for easy testing.
    set_target_properties(MinecraftBedrockAutoBot PROPERTIES
        VS_DEBUGGER_WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
    )
endif()
