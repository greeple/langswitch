@echo off
rem build.bat — локальная сборка под MSYS2 MinGW 32-bit.
rem Зависимости:  pacman -S mingw-w64-i686-gcc mingw-w64-i686-cmake mingw-w64-i686-ninja
rem Запускать из оболочки "MSYS2 MinGW 32-bit" (или добавить C:\msys64\mingw32\bin в PATH).

setlocal
where cmake >nul 2>nul || (echo cmake not found. Установите mingw-w64-i686-cmake. & exit /b 1)

cmake -G Ninja -B build -DCMAKE_BUILD_TYPE=Release || exit /b 1
cmake --build build || exit /b 1

echo.
echo Готово: build\LangSwitcher.exe
endlocal
