// tray.cpp — значок в области уведомлений и контекстное меню.
// Иконка отражает текущую раскладку активного окна: программа ищет файл
// icons\<LCID>.ico рядом с EXE (например 1033.ico — английская, 1049.ico —
// русская). Если файла нет — используется встроенная иконка из ресурсов.
// Shell_NotifyIcon с версией структуры для Windows 2000/XP.
#include "app.h"
#include "resource.h"

#include <map>
#include <stdio.h>

// объявлены здесь, используются main.cpp
void TrayOnCallback(WPARAM wParam, LPARAM lParam);
void TrayOnCommand(WPARAM wParam);
void TrayIconRecreate();
void TrayPollLayout();

#define TRAY_ICON_ID 1

// Команды меню трея
#define IDM_PAUSE      4001
#define IDM_SETTINGS   4002
#define IDM_AUTOSTART  4003
#define IDM_ABOUT      4004
#define IDM_EXIT       4005

static bool g_trayAdded = false;

// Кэш иконок
static HICON g_iconNormal = NULL;              // встроенная (нет файла языка)
static HICON g_iconPaused = NULL;              // встроенная «приостановлено»
static std::map<LANGID, HICON> g_langIcons;    // LCID -> иконка из icons\ (или NULL)
static HKL   g_lastHkl = (HKL)-1;              // для отслеживания смены раскладки

static std::wstring TrayExeDir()
{
    wchar_t path[MAX_PATH] = {0};
    GetModuleFileNameW(g_hInst, path, MAX_PATH);
    std::wstring s(path);
    size_t p = s.find_last_of(L"\\/");
    if (p != std::wstring::npos) s.erase(p);
    return s;
}

static HICON BuiltInIcon(bool paused)
{
    HICON* slot = paused ? &g_iconPaused : &g_iconNormal;
    if (!*slot) {
        *slot = (HICON)LoadImageW(g_hInst,
            MAKEINTRESOURCEW(paused ? IDI_APP_PAUSED : IDI_APP),
            IMAGE_ICON, GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON),
            LR_DEFAULTCOLOR);
    }
    return *slot;
}

// Иконка языка из icons\<LCID>.ico; кэшируется (включая «нет файла»)
static HICON LangIcon(LANGID lid)
{
    std::map<LANGID, HICON>::iterator it = g_langIcons.find(lid);
    if (it != g_langIcons.end()) return it->second;

    HICON h = NULL;
    wchar_t path[MAX_PATH * 2];
    int cx = GetSystemMetrics(SM_CXSMICON);
    int cy = GetSystemMetrics(SM_CYSMICON);

    // точное совпадение LCID: icons\1049.ico
    wsprintfW(path, L"%s\\icons\\%u.ico", TrayExeDir().c_str(), (unsigned)lid);
    h = (HICON)LoadImageW(NULL, path, IMAGE_ICON, cx, cy, LR_LOADFROMFILE);

    if (!h) {
        // запасной вариант: нейтральный подъязык (en-GB 2057 -> en 9... обычно
        // достаточно точного LCID, но вдруг лежит иконка базового языка)
        LANGID prim = MAKELANGID(PRIMARYLANGID(lid), SUBLANG_NEUTRAL);
        if (prim != lid) {
            wsprintfW(path, L"%s\\icons\\%u.ico", TrayExeDir().c_str(), (unsigned)prim);
            h = (HICON)LoadImageW(NULL, path, IMAGE_ICON, cx, cy, LR_LOADFROMFILE);
        }
    }

    LogF("Tray: icon for LCID %u -> %s", (unsigned)lid, h ? "loaded from icons\\" : "fallback");
    g_langIcons[lid] = h;
    return h;
}

// Раскладка активного окна
static HKL ForegroundLayout()
{
    HWND fg = GetForegroundWindow();
    DWORD tid = fg ? GetWindowThreadProcessId(fg, NULL) : 0;
    return GetKeyboardLayout(tid);
}

static void CurrentLangName(wchar_t* out, int cch)
{
    HKL hkl = ForegroundLayout();
    LANGID lid = (LANGID)(ULONG_PTR)hkl;
    out[0] = 0;
    GetLocaleInfoW(MAKELCID(lid, SORT_DEFAULT), LOCALE_SNATIVELANGUAGENAME, out, cch);
}

static void FillTip(NOTIFYICONDATAW& nid)
{
    ZeroMemory(&nid, sizeof(nid));
    nid.cbSize = NOTIFYICONDATAW_V2_SIZE; // размер структуры уровня XP (2000 совместим)
    nid.hWnd = g_hwndMain;
    nid.uID = TRAY_ICON_ID;
    nid.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP;
    nid.uCallbackMessage = WM_APP_TRAYICON;

    if (g_paused) {
        nid.hIcon = BuiltInIcon(true);
        lstrcpynW(nid.szTip, L"Lang Switcher (приостановлен)", ARRAYSIZE(nid.szTip));
        return;
    }

    HKL hkl = ForegroundLayout();
    g_lastHkl = hkl;
    HICON langIcon = LangIcon((LANGID)(ULONG_PTR)hkl);
    nid.hIcon = langIcon ? langIcon : BuiltInIcon(false);

    // подсказка: «Lang Switcher — русский»
    wchar_t lang[64];
    CurrentLangName(lang, 64);
    wchar_t tip[128];
    if (lang[0])
        wsprintfW(tip, L"Lang Switcher — %s", lang);
    else
        lstrcpynW(tip, L"Lang Switcher", ARRAYSIZE(tip));
    lstrcpynW(nid.szTip, tip, ARRAYSIZE(nid.szTip));
}

bool TrayIconAdd()
{
    NOTIFYICONDATAW nid;
    FillTip(nid);
    g_trayAdded = Shell_NotifyIconW(NIM_ADD, &nid) != FALSE;
    return g_trayAdded;
}

void TrayIconRemove()
{
    if (!g_trayAdded) return;
    NOTIFYICONDATAW nid;
    ZeroMemory(&nid, sizeof(nid));
    nid.cbSize = NOTIFYICONDATAW_V2_SIZE;
    nid.hWnd = g_hwndMain;
    nid.uID = TRAY_ICON_ID;
    Shell_NotifyIconW(NIM_DELETE, &nid);
    g_trayAdded = false;
}

void TrayIconUpdate()
{
    NOTIFYICONDATAW nid;
    FillTip(nid);
    if (g_trayAdded) Shell_NotifyIconW(NIM_MODIFY, &nid);
    else TrayIconAdd();
}

// Вызывается по таймеру из главного окна: если раскладка активного окна
// сменилась — обновить иконку и подсказку.
void TrayPollLayout()
{
    HKL hkl = ForegroundLayout();
    if (hkl != g_lastHkl) TrayIconUpdate(); // FillTip запомнит новый g_lastHkl
}

// Пересоздать значок (например, после перезапуска Проводника)
void TrayIconRecreate()
{
    g_trayAdded = false;
    g_lastHkl = (HKL)-1;
    TrayIconAdd();
}

static HMENU BuildTrayMenu()
{
    HMENU menu = CreatePopupMenu();
    AppendMenuW(menu, MF_STRING | (g_paused ? MF_CHECKED : 0), IDM_PAUSE,
                L"Приостановить");
    AppendMenuW(menu, MF_STRING, IDM_SETTINGS, L"Настройки…");
    AppendMenuW(menu, MF_SEPARATOR, 0, NULL);
    AppendMenuW(menu, MF_STRING | (g_settings.autostart ? MF_CHECKED : 0), IDM_AUTOSTART,
                L"Запускать вместе с Windows");
    AppendMenuW(menu, MF_SEPARATOR, 0, NULL);
    AppendMenuW(menu, MF_STRING, IDM_ABOUT, L"О программе…");
    AppendMenuW(menu, MF_STRING, IDM_EXIT, L"Выход");
    SetMenuDefaultItem(menu, IDM_SETTINGS, FALSE);
    return menu;
}

static void ShowAbout()
{
    MessageBoxW(g_hwndMain,
        L"Lang Switcher 1.0.0\r\n\r\n"
        L"Переключатель раскладки, который исправляет уже набранное слово.\r\n"
        L"Нажмите Break — слово перепечатается в правильной раскладке,\r\n"
        L"нажмите ещё раз — вернётся исходное.\r\n\r\n"
        L"Портативная программа: настройки (config.ini) и иконки раскладок\r\n"
        L"(папка icons\\, по имени языка: 1033.ico, 1049.ico…) лежат рядом с EXE.\r\n\r\n"
        L"Требуется Windows XP или новее (32/64 бит).\r\n"
        L"Свободное ПО (лицензия MIT).",
        L"О программе", MB_OK | MB_ICONINFORMATION);
}

// Обработка сообщений значка (вызывается из wndproc)
void TrayOnCallback(WPARAM wParam, LPARAM lParam)
{
    (void)wParam;
    UINT evt = (UINT)lParam;

    if (evt == WM_RBUTTONUP || evt == WM_CONTEXTMENU) {
        POINT pt;
        GetCursorPos(&pt);
        HMENU menu = BuildTrayMenu();
        SetForegroundWindow(g_hwndMain); // чтобы меню корректно закрывалось
        TrackPopupMenu(menu, TPM_RIGHTBUTTON | TPM_BOTTOMALIGN, pt.x, pt.y, 0, g_hwndMain, NULL);
        PostMessageW(g_hwndMain, WM_NULL, 0, 0);
        DestroyMenu(menu);
    } else if (evt == WM_LBUTTONDBLCLK) {
        ShowSettingsDialog();
    } else if (evt == WM_LBUTTONUP) {
        // одиночный клик — ничего (как у большинства нативных программ)
    }
}

// Обработка команд меню
void TrayOnCommand(WPARAM wParam)
{
    switch (LOWORD(wParam)) {
    case IDM_PAUSE:
        TogglePaused();
        break;
    case IDM_SETTINGS:
        ShowSettingsDialog();
        break;
    case IDM_AUTOSTART:
        g_settings.autostart = !g_settings.autostart;
        ApplyAutostart(g_settings.autostart);
        SaveSettings();
        break;
    case IDM_ABOUT:
        ShowAbout();
        break;
    case IDM_EXIT:
        DestroyWindow(g_hwndMain);
        break;
    }
}
