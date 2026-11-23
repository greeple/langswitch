@echo off
rem build.bat — локальная сборка компилятором MinGW-w64 (32-bit).
rem
rem ВАЖНО про pacman: pacman — это пакетный менеджер MSYS2 (Unix-оболочки для
rem Windows), а НЕ утилита Windows. В обычных cmd/PowerShell его нет, и этот
rem скрипт его не вызывает. Строка ниже — лишь подсказка, КАК поставить
rem инструмент один раз, если у вас установлен MSYS2:
rem     pacman -S mingw-w64-i686-gcc mingw-w64-i686-cmake mingw-w64-i686-ninja
rem Запускать build.bat нужно из оболочки "MSYS2 MinGW 32-bit" либо из cmd,
rem где в PATH добавлен каталог MinGW (например C:\msys64\mingw32\bin).
rem Если MSYS2 нет и ставить не хочется — локальная сборка не нужна:
rem готовый EXE собирает GitHub Actions (см. .github/workflows/build.yml).

setlocal
where cmake >nul 2>nul || (echo cmake not found in PATH. Нужен MinGW-w64 i686 + CMake ^(MSYS2: pacman -S mingw-w64-i686-cmake^) или сборка через GitHub Actions. & exit /b 1)

cmake -G Ninja -B build -DCMAKE_BUILD_TYPE=Release || exit /b 1
cmake --build build || exit /b 1

echo.
echo Готово: build\LangSwitcher.exe
endlocal
