// hook.cpp — низкоуровневые хуки клавиатуры и мыши (WH_KEYBOARD_LL / WH_MOUSE_LL,
// доступны с Windows 2000). Запоминает НАЖАТЫЕ КЛАВИШИ (не текст) с момента
// последнего действия, распознаёт горячую клавишу и двойное нажатие Shift.
#include "app.h"

static HHOOK g_kbHook = NULL;
static HHOOK g_mouseHook = NULL;

// Собственная копия клавиатурного состояния: в LL-хуке GetKeyboardState()
// может ещё не отражать текущее событие, поэтому ведём состояние сами.
static BYTE g_keyState[256];
static bool g_stateInited = false;

// Запись клавиш текущего «слова»
static std::vector<KeyEntry> g_record;

// Захват горячей клавиши в окне настроек
HWND  g_captureHwnd   = NULL;
bool  g_captureActive = false;

// Для двойного нажатия Shift
static DWORD g_lastShiftDown = 0;
static bool  g_shiftTapArmed = false;

// Глотаем ли клавишу до её отпускания (нажали горячую клавишу)
static UINT g_swallowUpVk = 0;

static bool IsModifierVk(UINT vk)
{
    return vk == VK_LSHIFT || vk == VK_RSHIFT || vk == VK_SHIFT ||
           vk == VK_LCONTROL || vk == VK_RCONTROL || vk == VK_CONTROL ||
           vk == VK_LMENU || vk == VK_RMENU || vk == VK_MENU ||
           vk == VK_CAPITAL;
}

static bool IsNavigationVk(UINT vk)
{
    return vk == VK_ESCAPE || vk == VK_RETURN || vk == VK_TAB ||
           vk == VK_LEFT || vk == VK_RIGHT || vk == VK_UP || vk == VK_DOWN ||
           vk == VK_HOME || vk == VK_END || vk == VK_PRIOR || vk == VK_NEXT ||
           vk == VK_DELETE || vk == VK_INSERT ||
           vk == VK_LWIN || vk == VK_RWIN || vk == VK_APPS;
}

static void InitKeyState()
{
    if (!g_stateInited) {
        GetKeyboardState(g_keyState);
        g_stateInited = true;
    }
}

// Обновить собственное состояние по событию хука
static void UpdateKeyState(UINT vk, bool down)
{
    switch (vk) {
    case VK_LSHIFT: case VK_RSHIFT:
        if (down) g_keyState[VK_SHIFT] |= 0x80; else g_keyState[VK_SHIFT] &= ~0x80;
        break;
    case VK_LCONTROL: case VK_RCONTROL:
        if (down) g_keyState[VK_CONTROL] |= 0x80; else g_keyState[VK_CONTROL] &= ~0x80;
        break;
    case VK_LMENU: case VK_RMENU:
        if (down) g_keyState[VK_MENU] |= 0x80; else g_keyState[VK_MENU] &= ~0x80;
        break;
    case VK_CAPITAL:
        if (down) g_keyState[VK_CAPITAL] ^= 0x01; // переключатель
        break;
    }
    if (down) g_keyState[vk] |= 0x80; else g_keyState[vk] &= ~0x80;
}

static bool IsDown(UINT vk) { return (g_keyState[vk] & 0x80) != 0; }

void ClearRecord()
{
    EnterCriticalSection(&g_stateCs);
    g_record.clear();
    LeaveCriticalSection(&g_stateCs);
}

size_t RecordSize()
{
    EnterCriticalSection(&g_stateCs);
    size_t n = g_record.size();
    LeaveCriticalSection(&g_stateCs);
    return n;
}

std::vector<KeyEntry> TakeRecordSnapshot()
{
    EnterCriticalSection(&g_stateCs);
    std::vector<KeyEntry> copy = g_record;
    g_record.clear();
    LeaveCriticalSection(&g_stateCs);
    return copy;
}

// Проверка совпадения с настроенной горячей клавишей (режим «нажатие»)
static bool HotkeyMatches(UINT vk)
{
    if (g_settings.hotkey.doubleTap) return false;
    if ((int)vk != g_settings.hotkey.vk) return false;

    DWORD mods = g_settings.hotkey.mods;
    bool wantCtrl  = (mods & MOD_CONTROL) != 0;
    bool wantAlt   = (mods & MOD_ALT) != 0;
    bool wantShift = (mods & MOD_SHIFT) != 0;

    bool ctrl  = IsDown(VK_CONTROL);
    bool alt   = IsDown(VK_MENU);
    bool shift = IsDown(VK_SHIFT);

    // Ctrl+Alt считается AltGr только если так настроено; для горячей клавиши
    // требуем точного совпадения модификаторов.
    return ctrl == wantCtrl && alt == wantAlt && shift == wantShift;
}

// Сколько символов породит нажатие (с учётом «мёртвых» клавиш)
static int CountChars(UINT vk, UINT scan, HKL hkl)
{
    // Копия состояния + текущие модификаторы уже в g_keyState
    BYTE state[256];
    memcpy(state, g_keyState, sizeof(state));

    wchar_t buf[8] = {0};
    int n = ToUnicodeEx(vk, scan, state, buf, 8, 0, hkl);
    // n < 0 — мёртвая клавиша (символов пока нет, состояние она уже изменила)
    return n > 0 ? n : 0;
}

// Обработка нажатия (не модификатора). Возвращает true, если событие нужно проглотить.
static bool ProcessKeyDown(const KBDLLHOOKSTRUCT* kb)
{
    UINT vk = kb->vkCode;

    // --- режим захвата горячей клавиши в диалоге настроек ---
    if (g_captureActive && g_captureHwnd) {
        // упаковываем состояние модификаторов в старшие биты wParam
        DWORD extra = 0;
        if (IsDown(VK_CONTROL)) extra |= 0x10000;
        if (IsDown(VK_MENU))    extra |= 0x20000;
        if (IsDown(VK_SHIFT))   extra |= 0x40000;
        PostMessageW(g_captureHwnd, WM_APP_CAPTUREKEY, (WPARAM)(vk | extra),
                     MAKELPARAM(kb->scanCode, kb->flags & LLKHF_EXTENDED ? 1 : 0));
        g_swallowUpVk = vk;
        return true; // глотим всё, пока идёт захват
    }

    // --- горячая клавиша: одиночное нажатие ---
    if (!g_settings.hotkey.doubleTap && !g_paused && HotkeyMatches(vk)) {
        PostMessageW(g_hwndMain, WM_APP_TRIGGER, 0, 0);
        g_swallowUpVk = vk;
        return true;
    }

    // --- горячая клавиша: двойное нажатие Shift ---
    if (g_settings.hotkey.doubleTap &&
        (vk == VK_LSHIFT || vk == VK_RSHIFT || vk == VK_SHIFT)) {
        DWORD now = GetTickCount();
        if (g_shiftTapArmed && now - g_lastShiftDown < 400 && !g_paused) {
            g_shiftTapArmed = false;
            PostMessageW(g_hwndMain, WM_APP_TRIGGER, 0, 0);
            g_swallowUpVk = vk;
            return true;
        }
        g_shiftTapArmed = true;
        g_lastShiftDown = now;
        return false; // обычный Shift — не глотим
    }

    // Любой другой клавишей сбрасываем «вооружение» двойного Shift
    g_shiftTapArmed = false;

    // Модификаторы сами по себе не записываем и словом не считаем
    // (CapsLock — граница слова: он меняет смысл уже набранного).
    if (IsModifierVk(vk)) {
        if (vk == VK_CAPITAL) {
            ClearRecord();
            ClearLastConversion();
        }
        return false;
    }

    // --- запись клавиш ---
    if (g_paused) return false;

    // В поле пароля не запоминаем вообще ничего (как в README)
    if (IsPasswordField()) {
        ClearRecord();
        ClearLastConversion();
        return false;
    }

    bool ctrl = IsDown(VK_CONTROL);
    bool alt  = IsDown(VK_MENU);
    bool altgr = ctrl && alt; // AltGr в Windows приходит как LCtrl+RAlt

    if (vk == VK_BACK) {
        EnterCriticalSection(&g_stateCs);
        if (!g_record.empty()) {
            g_record.pop_back(); // стираем последнюю записанную клавишу
            if (g_record.empty()) g_shiftTapArmed = false;
        } else {
            // Backspace вне записи — слово правили вручную, конвертацию отменяем
            ClearLastConversion();
        }
        LeaveCriticalSection(&g_stateCs);
        return false; // пропускаем в приложение
    }

    if (IsNavigationVk(vk)) {
        // Escape, стрелки, Enter и т.п. — граница слова
        ClearRecord();
        ClearLastConversion();
        return false;
    }

    if ((ctrl && !alt) || (alt && !ctrl)) {
        // Ctrl+C, Alt+F4 и прочие команды — не текст
        ClearRecord();
        ClearLastConversion();
        return false;
    }

    // Кандидат в текст: считаем, сколько символов он породит
    HWND fg = GetForegroundWindow();
    DWORD tid = fg ? GetWindowThreadProcessId(fg, NULL) : 0;
    HKL hkl = GetKeyboardLayout(tid);

    int chars = CountChars(vk, kb->scanCode, hkl);
    if (chars <= 0) {
        // F-клавиши и прочие непечатаемые — граница слова
        ClearRecord();
        ClearLastConversion();
        return false;
    }

    KeyEntry e;
    e.vk       = vk;
    e.scan     = kb->scanCode;
    e.extended = (kb->flags & LLKHF_EXTENDED) != 0;
    e.shift    = IsDown(VK_SHIFT);
    e.altgr    = altgr;
    e.chars    = chars;

    EnterCriticalSection(&g_stateCs);
    // новое нажатие после конвертации — «возврат назад» больше недоступен
    ClearLastConversion();
    g_record.push_back(e);
    LeaveCriticalSection(&g_stateCs);
    return false;
}

static LRESULT CALLBACK LowLevelKeyboardProc(int nCode, WPARAM wParam, LPARAM lParam)
{
    if (nCode == HC_ACTION) {
        KBDLLHOOKSTRUCT* kb = (KBDLLHOOKSTRUCT*)lParam;

        // Свои собственные впрыснутые события (SendInput) не обрабатываем,
        // иначе запись и состояние клавиш собьются.
        if (!(kb->flags & LLKHF_INJECTED)) {
            InitKeyState();
            bool down = (wParam == WM_KEYDOWN || wParam == WM_SYSKEYDOWN);

            if (down) {
                UpdateKeyState(kb->vkCode, true);
                if (ProcessKeyDown(kb)) return 1; // проглотить
            } else {
                // если нажатие глотали — глотим и отпускание той же клавиши
                if (g_swallowUpVk == kb->vkCode) {
                    g_swallowUpVk = 0;
                    UpdateKeyState(kb->vkCode, false);
                    return 1;
                }
                UpdateKeyState(kb->vkCode, false);
            }
        }
    }
    return CallNextHookEx(g_kbHook, nCode, wParam, lParam);
}

static LRESULT CALLBACK LowLevelMouseProc(int nCode, WPARAM wParam, LPARAM lParam)
{
    if (nCode == HC_ACTION) {
        if (wParam == WM_LBUTTONDOWN || wParam == WM_RBUTTONDOWN ||
            wParam == WM_MBUTTONDOWN) {
            // Клик — caret переместился, запись и «возврат» теряют смысл
            ClearRecord();
            ClearLastConversion();
            g_shiftTapArmed = false;
        }
    }
    return CallNextHookEx(g_mouseHook, nCode, wParam, lParam);
}

bool InstallHooks()
{
    InitKeyState();
    g_kbHook = SetWindowsHookExW(WH_KEYBOARD_LL, LowLevelKeyboardProc, g_hInst, 0);
    if (!g_kbHook) return false;
    g_mouseHook = SetWindowsHookExW(WH_MOUSE_LL, LowLevelMouseProc, g_hInst, 0);
    // хук мыши не критичен — без него всё равно работаем
    return true;
}

void UninstallHooks()
{
    if (g_kbHook)    { UnhookWindowsHookEx(g_kbHook);    g_kbHook = NULL; }
    if (g_mouseHook) { UnhookWindowsHookEx(g_mouseHook); g_mouseHook = NULL; }
}
