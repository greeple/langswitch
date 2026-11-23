// main.cpp — точка входа, скрытое окно, автозапуск, диспетчер сообщений.
#include "app.h"
#include "resource.h"

// tray.cpp
void TrayOnCallback(WPARAM wParam, LPARAM lParam);
void TrayOnCommand(WPARAM wParam);
void TrayIconRecreate();
void TrayPollLayout();

#define TIMER_LAYOUT_POLL 1

HINSTANCE        g_hInst = NULL;
HWND             g_hwndMain = NULL;
Settings         g_settings;
bool             g_paused = false;
CRITICAL_SECTION g_stateCs;

static UINT g_taskbarCreatedMsg = 0;

// ---------------------------------------------------------------------------
// Автозапуск (HKCU\Software\Microsoft\Windows\CurrentVersion\Run)
// ---------------------------------------------------------------------------
static const wchar_t* RUN_KEY = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
static const wchar_t* RUN_VALUE = L"LangSwitcher";

bool AutostartEnabled()
{
    HKEY hk;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, RUN_KEY, 0, KEY_READ, &hk) != ERROR_SUCCESS)
        return false;
    DWORD type = 0;
    LONG r = RegQueryValueExW(hk, RUN_VALUE, NULL, &type, NULL, NULL);
    RegCloseKey(hk);
    return r == ERROR_SUCCESS;
}

void ApplyAutostart(bool enable)
{
    HKEY hk;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, RUN_KEY, 0, KEY_SET_VALUE, &hk) != ERROR_SUCCESS)
        return;
    if (enable) {
        wchar_t exe[MAX_PATH * 2];
        GetModuleFileNameW(g_hInst, exe, MAX_PATH);
        std::wstring cmd = std::wstring(L"\"") + exe + L"\"";
        RegSetValueExW(hk, RUN_VALUE, 0, REG_SZ, (const BYTE*)cmd.c_str(),
                       (DWORD)((cmd.size() + 1) * sizeof(wchar_t)));
    } else {
        RegDeleteValueW(hk, RUN_VALUE);
    }
    RegCloseKey(hk);
}

// ---------------------------------------------------------------------------
// Пауза
// ---------------------------------------------------------------------------
void SetPaused(bool paused)
{
    g_paused = paused;
    if (paused) ClearRecord();
    ClearLastConversion();
    TrayIconUpdate();
}

void TogglePaused()
{
    SetPaused(!g_paused);
}

// ---------------------------------------------------------------------------
// Окно
// ---------------------------------------------------------------------------
static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    if (msg == g_taskbarCreatedMsg && g_taskbarCreatedMsg != 0) {
        // Проводник перезапущен — пересоздать значок
        TrayIconRecreate();
        return 0;
    }

    switch (msg) {
    case WM_APP_TRAYICON:
        TrayOnCallback(wParam, lParam);
        return 0;

    case WM_APP_TRIGGER:
        DoTrigger();
        return 0;

    case WM_TIMER:
        if (wParam == TIMER_LAYOUT_POLL) {
            TrayPollLayout(); // смена раскладки активного окна -> сменить иконку
            return 0;
        }
        break;

    case WM_COMMAND:
        TrayOnCommand(wParam);
        return 0;

    case WM_DESTROY:
        TrayIconRemove();
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

static HWND CreateHiddenWindow()
{
    WNDCLASSEXW wc;
    ZeroMemory(&wc, sizeof(wc));
    wc.cbSize        = sizeof(wc);
    wc.lpfnWndProc   = WndProc;
    wc.hInstance     = g_hInst;
    wc.hIcon         = LoadIconW(g_hInst, MAKEINTRESOURCEW(IDI_APP));
    wc.lpszClassName = L"LangSwitcherCppMainWnd";
    if (!RegisterClassExW(&wc)) return NULL;

    // Обычное скрытое окно (не HWND_MESSAGE) — чтобы получать широковещательное
    // сообщение TaskbarCreated после перезапуска Проводника.
    return CreateWindowExW(0, wc.lpszClassName, APP_NAME, WS_OVERLAPPED,
                           0, 0, 0, 0, NULL, NULL, g_hInst, NULL);
}

// ---------------------------------------------------------------------------
// WinMain
// ---------------------------------------------------------------------------
int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE, LPSTR, int)
{
    g_hInst = hInstance;

    // Единственный экземпляр
    HANDLE mutex = CreateMutexW(NULL, TRUE, APP_MUTEX_NAME);
    if (mutex == NULL || GetLastError() == ERROR_ALREADY_EXISTS) {
        MessageBoxW(NULL,
            L"Lang Switcher уже запущен (значок в области уведомлений).",
            L"Lang Switcher", MB_OK | MB_ICONINFORMATION);
        if (mutex) CloseHandle(mutex);
        return 0;
    }

    InitializeCriticalSection(&g_stateCs);
    InitCommonControls();

    LoadSettings();
    g_paused = g_settings.startPaused;
    LogInit();
    LogF("=== Lang Switcher started (pid=%lu, paused=%d) ===",
         GetCurrentProcessId(), g_paused ? 1 : 0);

    // Портативный режим: если config.ini ещё нет — сразу создаём его рядом
    // с EXE с настройками по умолчанию, чтобы пользователь видел файл.
    if (GetFileAttributesW(SettingsPath().c_str()) == INVALID_FILE_ATTRIBUTES) {
        SaveSettings();
        LogF("config.ini created at first start");
    }

    g_hwndMain = CreateHiddenWindow();
    if (!g_hwndMain) return 1;

    g_taskbarCreatedMsg = RegisterWindowMessageW(L"TaskbarCreated");

    if (!InstallHooks()) {
        MessageBoxW(NULL, L"Не удалось установить клавиатурный хук.",
                    L"Lang Switcher", MB_OK | MB_ICONERROR);
        return 1;
    }

    TrayIconAdd();
    // Синхронизировать флаг автозапуска с реальным состоянием реестра
    g_settings.autostart = AutostartEnabled();

    // Следить за раскладкой активного окна и обновлять иконку в трее
    SetTimer(g_hwndMain, TIMER_LAYOUT_POLL, 500, NULL);

    MSG m;
    while (GetMessageW(&m, NULL, 0, 0) > 0) {
        // Диалоги настроек модальны; PropertySheet сам качает сообщения,
        // поэтому здесь достаточно обычного цикла.
        TranslateMessage(&m);
        DispatchMessageW(&m);
    }

    UninstallHooks();
    SaveSettings();
    DeleteCriticalSection(&g_stateCs);
    CloseHandle(mutex);
    return 0;
}
