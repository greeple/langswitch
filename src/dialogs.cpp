// dialogs.cpp — окно настроек: классическое окно свойств Win32 (PropertySheet)
// с тремя вкладками: «Основные», «Клавиша», «Раскладки». Выглядит нативно на
// любой Windows (с темой — при манифесте comctl32 v6).
#include "app.h"
#include "resource.h"

#include <prsht.h>

// ---------------------------------------------------------------------------
// Человекочитаемое имя клавиши
// ---------------------------------------------------------------------------
static std::wstring VkName(UINT vk, bool extended)
{
    wchar_t buf[64];

    if (vk >= VK_F1 && vk <= VK_F24) {
        wsprintfW(buf, L"F%u", vk - VK_F1 + 1);
        return buf;
    }
    if (vk >= 0x30 && vk <= 0x39) { // цифры
        buf[0] = (wchar_t)vk; buf[1] = 0;
        return buf;
    }
    if (vk >= 0x41 && vk <= 0x5A) { // латинские буквы
        buf[0] = (wchar_t)vk; buf[1] = 0;
        return buf;
    }
    if (vk >= VK_NUMPAD0 && vk <= VK_NUMPAD9) {
        wsprintfW(buf, L"Цифра %u", vk - VK_NUMPAD0);
        return buf;
    }
    switch (vk) {
    case VK_PAUSE:   return L"Break";
    case VK_CANCEL:  return L"Break";
    case VK_CAPITAL: return L"Caps Lock";
    case VK_SPACE:   return L"Пробел";
    case VK_SCROLL:  return L"Scroll Lock";
    case VK_PRIOR:   return L"Page Up";
    case VK_NEXT:    return L"Page Down";
    case VK_MULTIPLY:return L"* (цифр.)";
    case VK_ADD:     return L"+ (цифр.)";
    case VK_SUBTRACT:return L"- (цифр.)";
    case VK_DECIMAL: return L". (цифр.)";
    case VK_DIVIDE:  return L"/ (цифр.)";
    }

    // остальное — системное имя клавиши (локализованное)
    UINT scan = MapVirtualKeyW(vk, 0 /*MAPVK_VK_TO_VSC*/);
    if (scan) {
        LPARAM lp = (LPARAM)((scan << 16) | (extended ? (1u << 24) : 0));
        if (GetKeyNameTextW((LONG)lp, buf, 64) > 0) return buf;
    }
    wsprintfW(buf, L"Клавиша %u", vk);
    return buf;
}

static std::wstring ModsName(DWORD mods)
{
    std::wstring s;
    if (mods & MOD_CONTROL) s += L"Ctrl+";
    if (mods & MOD_ALT)     s += L"Alt+";
    if (mods & MOD_SHIFT)   s += L"Shift+";
    return s;
}

static std::wstring FormatHotkey(const HotkeyCfg& h)
{
    if (h.doubleTap) return L"Shift — двойное нажатие";
    return ModsName(h.mods) + VkName((UINT)h.vk, false);
}

// ---------------------------------------------------------------------------
// Вкладка «Основные»
// ---------------------------------------------------------------------------
static INT_PTR CALLBACK GeneralPageProc(HWND hDlg, UINT msg, WPARAM wParam, LPARAM lParam)
{
    (void)lParam;
    switch (msg) {
    case WM_INITDIALOG:
        CheckDlgButton(hDlg, IDC_START_WINDOWS, g_settings.autostart ? BST_CHECKED : BST_UNCHECKED);
        CheckDlgButton(hDlg, IDC_START_PAUSED, g_settings.startPaused ? BST_CHECKED : BST_UNCHECKED);
        return TRUE;

    case WM_COMMAND:
        switch (LOWORD(wParam)) {
        case IDC_START_WINDOWS:
            g_settings.autostart = (IsDlgButtonChecked(hDlg, IDC_START_WINDOWS) == BST_CHECKED);
            ApplyAutostart(g_settings.autostart);
            SaveSettings();
            return TRUE;
        case IDC_START_PAUSED:
            g_settings.startPaused = (IsDlgButtonChecked(hDlg, IDC_START_PAUSED) == BST_CHECKED);
            SaveSettings();
            return TRUE;
        }
        break;

    case WM_NOTIFY: {
        NMHDR* nm = (NMHDR*)lParam;
        if (nm->code == (UINT)PSN_KILLACTIVE) SetWindowLongPtrW(hDlg, DWLP_MSGRESULT, FALSE);
        return FALSE;
    }
    }
    return FALSE;
}

// ---------------------------------------------------------------------------
// Вкладка «Клавиша» (захват комбинации)
// ---------------------------------------------------------------------------
static bool g_capturing = false;

static void StartCapture(HWND hDlg)
{
    g_capturing = true;
    g_captureHwnd = hDlg;
    g_captureActive = true;
    SetWindowTextW(GetDlgItem(hDlg, IDC_HOTKEY_KEY), L"Нажмите клавишу или комбинацию…");
}

static void StopCapture(HWND hDlg)
{
    g_capturing = false;
    g_captureActive = false;
    g_captureHwnd = NULL;
    SetWindowTextW(GetDlgItem(hDlg, IDC_HOTKEY_KEY), FormatHotkey(g_settings.hotkey).c_str());
}

static bool IsBareModifier(UINT vk)
{
    return vk == VK_SHIFT || vk == VK_LSHIFT || vk == VK_RSHIFT ||
           vk == VK_CONTROL || vk == VK_LCONTROL || vk == VK_RCONTROL ||
           vk == VK_MENU || vk == VK_LMENU || vk == VK_RMENU ||
           vk == VK_LWIN || vk == VK_RWIN;
}

static INT_PTR CALLBACK HotkeyPageProc(HWND hDlg, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg) {
    case WM_INITDIALOG:
        SetWindowTextW(GetDlgItem(hDlg, IDC_HOTKEY_KEY), FormatHotkey(g_settings.hotkey).c_str());
        CheckRadioButton(hDlg, IDC_TRIGGER_PRESS, IDC_TRIGGER_DBL,
                         g_settings.hotkey.doubleTap ? IDC_TRIGGER_DBL : IDC_TRIGGER_PRESS);
        return TRUE;

    case WM_APP_CAPTUREKEY: {
        if (!g_capturing) return TRUE;
        UINT vk = (UINT)(wParam & 0xFFFF);
        bool extended = (LOWORD(lParam) & 1) != 0;
        (void)extended;

        if (vk == VK_ESCAPE) { StopCapture(hDlg); return TRUE; }

        DWORD mods = 0;
        if (wParam & 0x10000) mods |= MOD_CONTROL;
        if (wParam & 0x20000) mods |= MOD_ALT;
        if (wParam & 0x40000) mods |= MOD_SHIFT;

        // Ctrl+Alt = AltGr: как модификаторы горячей клавиши их не принимаем
        if ((mods & MOD_CONTROL) && (mods & MOD_ALT)) {
            MessageBeep(MB_ICONWARNING);
            return TRUE; // захват продолжается
        }
        // «голый» модификатор в режиме одиночного нажатия не годится
        if (IsBareModifier(vk)) {
            MessageBeep(MB_ICONWARNING);
            return TRUE;
        }

        g_settings.hotkey.vk = (int)vk;
        g_settings.hotkey.mods = mods;
        g_settings.hotkey.doubleTap = false;
        CheckRadioButton(hDlg, IDC_TRIGGER_PRESS, IDC_TRIGGER_DBL, IDC_TRIGGER_PRESS);
        SaveSettings();
        StopCapture(hDlg);
        return TRUE;
    }

    case WM_COMMAND:
        switch (LOWORD(wParam)) {
        case IDC_HOTKEY_KEY:
            if (HIWORD(wParam) == EN_SETFOCUS && !g_capturing) StartCapture(hDlg);
            return TRUE;
        case IDC_TRIGGER_PRESS:
            if (g_settings.hotkey.doubleTap) {
                g_settings.hotkey.doubleTap = false;
                g_settings.hotkey.vk = VK_PAUSE;
                g_settings.hotkey.mods = 0;
                SaveSettings();
                SetWindowTextW(GetDlgItem(hDlg, IDC_HOTKEY_KEY),
                               FormatHotkey(g_settings.hotkey).c_str());
            }
            return TRUE;
        case IDC_TRIGGER_DBL:
            if (!g_settings.hotkey.doubleTap) {
                g_settings.hotkey.doubleTap = true;
                g_settings.hotkey.vk = VK_SHIFT;
                g_settings.hotkey.mods = 0;
                SaveSettings();
                SetWindowTextW(GetDlgItem(hDlg, IDC_HOTKEY_KEY),
                               FormatHotkey(g_settings.hotkey).c_str());
            }
            return TRUE;
        case IDC_HOTKEY_RESET:
            g_settings.hotkey.vk = VK_PAUSE;
            g_settings.hotkey.mods = 0;
            g_settings.hotkey.doubleTap = false;
            CheckRadioButton(hDlg, IDC_TRIGGER_PRESS, IDC_TRIGGER_DBL, IDC_TRIGGER_PRESS);
            SaveSettings();
            StopCapture(hDlg);
            return TRUE;
        }
        break;

    case WM_DESTROY:
        if (g_capturing) { g_captureActive = false; g_captureHwnd = NULL; g_capturing = false; }
        return TRUE;
    }
    return FALSE;
}

// ---------------------------------------------------------------------------
// Вкладка «Раскладки» (цикл переключения)
// ---------------------------------------------------------------------------
static std::vector<std::wstring> g_cycleKlid;   // параллельно элементам списка
static std::vector<std::wstring> g_availKlid;   // параллельно элементам комбо
static std::vector<HKL>          g_availHkl;

static void RebuildCycleList(HWND hDlg)
{
    HWND lb = GetDlgItem(hDlg, IDC_CYCLE_LIST);
    SendMessageW(lb, LB_RESETCONTENT, 0, 0);
    g_cycleKlid.clear();
    for (size_t i = 0; i < g_settings.cycle.size(); ++i) {
        HKL h = KlidToHkl(g_settings.cycle[i]);
        if (!h) continue;
        std::wstring name = LayoutDisplayName(h) + L"  [" + g_settings.cycle[i] + L"]";
        SendMessageW(lb, LB_ADDSTRING, 0, (LPARAM)name.c_str());
        g_cycleKlid.push_back(g_settings.cycle[i]);
    }
    if (SendMessageW(lb, LB_GETCOUNT, 0, 0) > 0)
        SendMessageW(lb, LB_SETCURSEL, 0, 0);
}

static void CommitCycle(HWND hDlg)
{
    (void)hDlg;
    g_settings.cycle = g_cycleKlid;
    if (g_settings.cycle.size() < 2) {
        // меньше двух — работать нечему, восстановим все установленные
        g_settings.cycle.clear();
        std::vector<HKL> all = GetInstalledLayouts();
        for (size_t i = 0; i < all.size(); ++i)
            g_settings.cycle.push_back(HklToKlid(all[i]));
    }
    SaveSettings();
}

static INT_PTR CALLBACK LayoutsPageProc(HWND hDlg, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg) {
    case WM_INITDIALOG: {
        HWND cb = GetDlgItem(hDlg, IDC_AVAILABLE);
        SendMessageW(cb, CB_RESETCONTENT, 0, 0);
        g_availKlid.clear();
        g_availHkl.clear();
        std::vector<HKL> all = GetInstalledLayouts();
        for (size_t i = 0; i < all.size(); ++i) {
            std::wstring klid = HklToKlid(all[i]);
            std::wstring name = LayoutDisplayName(all[i]) + L"  [" + klid + L"]";
            SendMessageW(cb, CB_ADDSTRING, 0, (LPARAM)name.c_str());
            g_availKlid.push_back(klid);
            g_availHkl.push_back(all[i]);
        }
        if (!g_availKlid.empty()) SendMessageW(cb, CB_SETCURSEL, 0, 0);
        RebuildCycleList(hDlg);
        return TRUE;
    }

    case WM_COMMAND: {
        WORD id = LOWORD(wParam);
        if (id == IDC_BTN_ADD) {
            HWND cb = GetDlgItem(hDlg, IDC_AVAILABLE);
            int sel = (int)SendMessageW(cb, CB_GETCURSEL, 0, 0);
            if (sel >= 0 && sel < (int)g_availKlid.size()) {
                bool dup = false;
                for (size_t i = 0; i < g_cycleKlid.size(); ++i)
                    if (_wcsicmp(g_cycleKlid[i].c_str(), g_availKlid[(size_t)sel].c_str()) == 0)
                        { dup = true; break; }
                if (!dup) {
                    HWND lb = GetDlgItem(hDlg, IDC_CYCLE_LIST);
                    std::wstring name = LayoutDisplayName(g_availHkl[(size_t)sel]) +
                                        L"  [" + g_availKlid[(size_t)sel] + L"]";
                    SendMessageW(lb, LB_ADDSTRING, 0, (LPARAM)name.c_str());
                    g_cycleKlid.push_back(g_availKlid[(size_t)sel]);
                    CommitCycle(hDlg);
                } else {
                    MessageBeep(MB_ICONWARNING);
                }
            }
            return TRUE;
        }
        if (id == IDC_BTN_REMOVE) {
            HWND lb = GetDlgItem(hDlg, IDC_CYCLE_LIST);
            int sel = (int)SendMessageW(lb, LB_GETCURSEL, 0, 0);
            if (sel >= 0 && sel < (int)g_cycleKlid.size()) {
                SendMessageW(lb, LB_DELETESTRING, (WPARAM)sel, 0);
                g_cycleKlid.erase(g_cycleKlid.begin() + sel);
                int cnt = (int)SendMessageW(lb, LB_GETCOUNT, 0, 0);
                if (cnt > 0) SendMessageW(lb, LB_SETCURSEL, (WPARAM)(sel < cnt ? sel : cnt - 1), 0);
                CommitCycle(hDlg);
            }
            return TRUE;
        }
        if (id == IDC_BTN_UP || id == IDC_BTN_DOWN) {
            HWND lb = GetDlgItem(hDlg, IDC_CYCLE_LIST);
            int sel = (int)SendMessageW(lb, LB_GETCURSEL, 0, 0);
            int cnt = (int)g_cycleKlid.size();
            int to = (id == IDC_BTN_UP) ? sel - 1 : sel + 1;
            if (sel >= 0 && to >= 0 && to < cnt) {
                std::wstring k = g_cycleKlid[(size_t)sel];
                g_cycleKlid.erase(g_cycleKlid.begin() + sel);
                g_cycleKlid.insert(g_cycleKlid.begin() + to, k);
                RebuildCycleList(hDlg);
                SendMessageW(lb, LB_SETCURSEL, (WPARAM)to, 0);
                CommitCycle(hDlg);
            }
            return TRUE;
        }
        break;
    }
    }
    return FALSE;
}

// ---------------------------------------------------------------------------
// Сборка окна свойств
// ---------------------------------------------------------------------------
void ShowSettingsDialog()
{
    // Пока открыты настройки — не конвертируем по горячей клавише,
    // чтобы диалог не мешал (захват клавиш работает через тот же хук).
    PROPSHEETPAGEW psp[3];
    ZeroMemory(psp, sizeof(psp));

    psp[0].dwSize = sizeof(PROPSHEETPAGEW);
    psp[0].dwFlags = PSP_DEFAULT | PSP_USETITLE;
    psp[0].hInstance = g_hInst;
    psp[0].pszTemplate = MAKEINTRESOURCEW(IDD_GENERAL);
    psp[0].pszTitle = L"Основные";
    psp[0].pfnDlgProc = GeneralPageProc;

    psp[1].dwSize = sizeof(PROPSHEETPAGEW);
    psp[1].dwFlags = PSP_DEFAULT | PSP_USETITLE;
    psp[1].hInstance = g_hInst;
    psp[1].pszTemplate = MAKEINTRESOURCEW(IDD_HOTKEY);
    psp[1].pszTitle = L"Клавиша";
    psp[1].pfnDlgProc = HotkeyPageProc;

    psp[2].dwSize = sizeof(PROPSHEETPAGEW);
    psp[2].dwFlags = PSP_DEFAULT | PSP_USETITLE;
    psp[2].hInstance = g_hInst;
    psp[2].pszTemplate = MAKEINTRESOURCEW(IDD_LAYOUTS);
    psp[2].pszTitle = L"Раскладки";
    psp[2].pfnDlgProc = LayoutsPageProc;

    PROPSHEETHEADERW psh;
    ZeroMemory(&psh, sizeof(psh));
    psh.dwSize = sizeof(PROPSHEETHEADERW);
    psh.dwFlags = PSH_PROPSHEETPAGE | PSH_NOAPPLYNOW | PSH_NOCONTEXTHELP;
    psh.hwndParent = g_hwndMain;
    psh.hInstance = g_hInst;
    psh.pszCaption = L"Настройки Lang Switcher";
    psh.nPages = 3;
    psh.ppsp = psp;

    PropertySheetW(&psh);
}
