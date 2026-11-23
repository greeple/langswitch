# CMakeLists.txt — сборка Lang Switcher (32-битный Windows, XP-совместимый)
cmake_minimum_required(VERSION 3.16)
project(LangSwitcher VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT WIN32 AND NOT MINGW)
    message(FATAL_ERROR "This project targets Windows only (use a mingw-w64 toolchain).")
endif()

# Только API уровня Windows XP (0x0501); на практике код использует лишь то,
# что есть уже в Windows 2000 (LL-хуки, SendInput, ToUnicodeEx, Shell_NotifyIcon).
add_compile_definitions(
    UNICODE _UNICODE
    _WIN32_WINNT=0x0501
    WINVER=0x0501
    WIN32_LEAN_AND_MEAN
    NOMINMAX
)

set(SOURCES
    src/main.cpp
    src/settings.cpp
    src/hook.cpp
    src/layouts.cpp
    src/convert.cpp
    src/action.cpp
    src/tray.cpp
    src/dialogs.cpp
    src/log.cpp
    src/compat.cpp
    src/ui.rc
)

add_executable(LangSwitcher WIN32 ${SOURCES})
target_include_directories(LangSwitcher PRIVATE src)
target_link_libraries(LangSwitcher PRIVATE user32 gdi32 shell32 comctl32 advapi32)

if(MINGW)
    target_compile_options(LangSwitcher PRIVATE
        -Wall -Wextra -Wno-unused-parameter
        -ffunction-sections -fdata-sections
        $<$<CONFIG:Release>:-O2>
    )
    # Статическая линковка рантайма (без DLL-зависимостей кроме системных),
    # версия подсистемы 5.01 — запуск на Windows XP.
    target_link_options(LangSwitcher PRIVATE
        -static
        -Wl,--major-os-version,5
        -Wl,--minor-os-version,1
        -Wl,--major-subsystem-version,5
        -Wl,--minor-subsystem-version,1
        $<$<CONFIG:Release>:-Wl,--gc-sections>
    )
    if(CMAKE_BUILD_TYPE STREQUAL "Release")
        add_custom_command(TARGET LangSwitcher POST_BUILD
            COMMAND ${CMAKE_STRIP} $<TARGET_FILE:LangSwitcher>)
    endif()
elseif(MSVC)
    target_compile_options(LangSwitcher PRIVATE /W4 /permissive-)
    # MSVC: XP-совместимость требует тулсета v141_xp; на современных VS
    # используйте MinGW-сборку (см. .github/workflows/build.yml).
    target_link_options(LangSwitcher PRIVATE /SUBSYSTEM:WINDOWS,5.01)
endif()

set_target_properties(LangSwitcher PROPERTIES OUTPUT_NAME "LangSwitcher")
