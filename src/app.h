// app.h — общие объявления Lang Switcher (C++).
// Требуются только API уровня Windows 2000/XP (_WIN32_WINNT=0x0501).
#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0501
#endif
#ifndef WINVER
#define WINVER 0x0501
#endif

#include <windows.h>
#include <commctrl.h>
#include <shellapi.h>
#include <shlobj.h>

#include <string>
#include <vector>

#define APP_NAME        L"Lang Switcher"
#define APP_MUTEX_NAME  L"LangSwitcherCpp_SingleInstance_Mutex"
#define APP_VERSION_STR "1.0.0"

// Приватные сообщения главного (скрытого) окна
#define WM_APP_TRAYICON   (WM_APP + 1)   // wParam: события значка в трее
#define WM_APP_TRIGGER    (WM_APP + 2)   // сработала горячая клавиша (из хука)
#define WM_APP_CAPTUREKEY (WM_APP + 3)   // захват горячей клавиши в диалоге настроек

// Один записанный нажатый символ-клавиша (клавиши, а не текст — как в README)
struct KeyEntry {
    UINT vk;          // виртуальный код
    UINT scan;        // скан-код (для повторного набора через KEYEVENTF_SCANCODE)
    bool extended;    // расширенный флаг скан-кода
    bool shift;       // был ли зажат Shift
    bool altgr;       // был ли зажат AltGr (Ctrl+Alt)
    int  chars;       // сколько символов породило нажатие (по ToUnicodeEx)
};

// Настройки (config.ini)
struct HotkeyCfg {
    int  vk        = VK_PAUSE;   // виртуальный код клавиши
    DWORD mods     = 0;          // MOD_CONTROL / MOD_ALT / MOD_SHIFT
    bool doubleTap = false;      // режим «двойное нажатие Shift»
};

struct Settings {
    bool autostart   = false;    // запуск вместе с Windows
    bool startPaused = false;    // запускать в режиме «приостановлено»
    bool debugLog    = false;    // писать debug.log рядом с настройками
    HotkeyCfg hotkey;
    std::vector<std::wstring> cycle; // KLID-строки раскладок цикла (например "00000409")
};

// --- глобальное состояние (main.cpp) ---
extern HINSTANCE   g_hInst;
extern HWND        g_hwndMain;      // скрытое окно: трей + приём сообщений
extern Settings    g_settings;
extern bool        g_paused;        // текущий режим «приостановлено»
extern CRITICAL_SECTION g_stateCs;  // защита записи клавиш и состояния конвертации

// main.cpp
void ApplyAutostart(bool enable);   // реестр HKCU\...\Run
bool AutostartEnabled();
void SetPaused(bool paused);        // обновляет g_paused и значок в трее
void TogglePaused();

// settings.cpp
std::wstring SettingsPath();        // config.ini рядом с EXE (программа портативная)
void LoadSettings();
void SaveSettings();

// hook.cpp
bool InstallHooks();
void UninstallHooks();
void ClearRecord();                       // сброс записанных клавиш
size_t RecordSize();
std::vector<KeyEntry> TakeRecordSnapshot(); // копия + очистка
// захват горячей клавиши в диалоге:
extern HWND  g_captureHwnd;               // окно, которому слать WM_APP_CAPTUREKEY (0 = выкл)
extern bool  g_captureActive;

// layouts.cpp
std::vector<HKL> GetInstalledLayouts();
std::wstring HklToKlid(HKL hkl);
HKL          KlidToHkl(const std::wstring& klid);   // NULL, если не установлена
std::wstring LayoutDisplayName(HKL hkl);
HKL          NextLayoutInCycle(HKL current);        // следующая по циклу из настроек
bool         RequestLayoutSwitch(HWND fgWnd, HKL target, bool wait); // WM_INPUTLANGCHANGEREQUEST

// convert.cpp — таблицы раскладок для конвертации выделенного текста
struct LayoutMap {
    // (vk, модификаторы) -> строка, которую печатает клавиша
    std::vector<std::pair<std::pair<UINT, DWORD>, std::wstring>> keyToChar;
    // символ/строка -> (vk, модификаторы) в этой раскладке
    std::vector<std::pair<std::wstring, std::pair<UINT, DWORD>>> charToKey;
};
const LayoutMap& GetLayoutMap(HKL hkl);
std::wstring ConvertText(const std::wstring& text, HKL src, HKL dst);

// action.cpp — всё, что происходит по нажатию горячей клавиши
void DoTrigger();          // вызывается из wndproc главного окна (запускает рабочий поток)
void ClearLastConversion(); // сброс состояния «можно вернуть назад»
bool IsPasswordField();    // фокус в поле ввода пароля

// tray.cpp
bool TrayIconAdd();
void TrayIconRemove();
void TrayIconUpdate();     // значок/подсказка под текущее состояние

// dialogs.cpp
void ShowSettingsDialog(); // модальное окно свойств (вкладка «Основные», «Клавиша», «Раскладки»)

// log.cpp — диагностика (включается debug_log=1 в config.ini)
extern bool g_logEnabled;
void LogInit();
void LogF(const char* fmt, ...);
