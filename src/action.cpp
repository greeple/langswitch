// action.cpp — действие по горячей клавише:
//  1) набрано слово  -> стереть и перепечатать ТЕ ЖЕ КЛАВИШИ в другой раскладке;
//     повторное нажатие возвращает исходное (клавиши хранятся, не текст);
//  2) ничего не набрано, но есть выделение -> конвертировать выделенный текст
//     (буфер обмена восстанавливается полностью, все форматы);
//  3) ничего нет -> просто переключить раскладку на следующую по циклу.
// В поле пароля — только переключение раскладки, запись клавиш не ведётся.
#include "app.h"

#include <wctype.h>
#include <string>
#include <stdio.h>

// ---------------------------------------------------------------------------
// Состояние «последняя конвертация» (для возврата назад)
// ---------------------------------------------------------------------------
struct LastConversion {
    bool active;
    bool reverted;              // сейчас показано исходное (true) или конвертированное (false)
    std::vector<KeyEntry> keys; // сохранённые НАЖАТИЯ (не текст)
    HKL src;                    // раскладка, в которой набирали
    HKL dst;                    // раскладка, в которую перепечатали
    int charsSrc;               // сколько символов дают эти клавиши в src
    int charsDst;               // сколько символов дали в dst
    LastConversion() : active(false), reverted(false), src(NULL), dst(NULL),
                       charsSrc(0), charsDst(0) {}
};
static LastConversion g_last;

void ClearLastConversion()
{
    EnterCriticalSection(&g_stateCs);
    g_last.active = false;
    g_last.keys.clear();
    LeaveCriticalSection(&g_stateCs);
}

// ---------------------------------------------------------------------------
// Поле пароля
// ---------------------------------------------------------------------------
bool IsPasswordField()
{
    HWND fg = GetForegroundWindow();
    if (!fg) return false;
    DWORD tid = GetWindowThreadProcessId(fg, NULL);
    GUITHREADINFO gti;
    ZeroMemory(&gti, sizeof(gti));
    gti.cbSize = sizeof(gti);
    if (!GetGUIThreadInfo(tid, &gti)) return false;

    HWND h = gti.hwndFocus ? gti.hwndFocus : gti.hwndActive;
    if (!h) return false;

    wchar_t cls[64] = {0};
    if (GetClassNameW(h, cls, 64) <= 0) return false;

    bool editLike = (_wcsicmp(cls, L"Edit") == 0) ||
                    (_wcsnicmp(cls, L"RichEdit", 8) == 0) ||
                    (_wcsicmp(cls, L"RICHEDIT50W") == 0);
    if (!editLike) return false;

    LONG_PTR style = GetWindowLongPtrW(h, GWL_STYLE);
    return (style & ES_PASSWORD) != 0;
}

// ---------------------------------------------------------------------------
// Вспомогательные функции ввода
// ---------------------------------------------------------------------------
static volatile LONG g_busy = 0; // идёт ли сейчас конвертация

// Подождать, пока пользователь отпустит модификаторы (иначе впрыск «склеится»
// с физически зажатыми Shift/Ctrl/Alt).
static void WaitForModifiersUp(int timeoutMs)
{
    static const UINT vks[] = { VK_SHIFT, VK_CONTROL, VK_MENU, VK_LWIN, VK_RWIN };
    DWORD start = GetTickCount();
    for (;;) {
        bool any = false;
        for (int i = 0; i < 5; ++i)
            if (GetAsyncKeyState((int)vks[i]) & 0x8000) { any = true; break; }
        if (!any) return;
        if ((DWORD)(GetTickCount() - start) > (DWORD)timeoutMs) return;
        Sleep(10);
    }
}

static void AddVk(std::vector<INPUT>& v, UINT vk, bool up, bool extended = false)
{
    INPUT in;
    ZeroMemory(&in, sizeof(in));
    in.type = INPUT_KEYBOARD;
    in.ki.wVk = (WORD)vk;
    in.ki.dwFlags = (up ? KEYEVENTF_KEYUP : 0) | (extended ? KEYEVENTF_EXTENDEDKEY : 0);
    v.push_back(in);
}

static void AddScan(std::vector<INPUT>& v, UINT scan, bool up, bool extended)
{
    INPUT in;
    ZeroMemory(&in, sizeof(in));
    in.type = INPUT_KEYBOARD;
    in.ki.wScan = (WORD)scan;
    in.ki.dwFlags = KEYEVENTF_SCANCODE | (up ? KEYEVENTF_KEYUP : 0) |
                    (extended ? KEYEVENTF_EXTENDEDKEY : 0);
    v.push_back(in);
}

static void SendAll(std::vector<INPUT>& v)
{
    if (v.empty()) return;
    // SendInput может принять не всё (блокировка ввода) — повторяем остаток
    size_t sent = 0;
    for (int attempt = 0; attempt < 3 && sent < v.size(); ++attempt) {
        UINT n = SendInput((UINT)(v.size() - sent), &v[sent], sizeof(INPUT));
        if (n == 0) {
            LogF("SendAll: SendInput failed (events=%u, sent=%u, err=%lu)",
                 (unsigned)(v.size() - sent), (unsigned)sent, GetLastError());
            Sleep(30);
            continue;
        }
        sent += n;
    }
    if (sent < v.size())
        LogF("SendAll: INCOMPLETE %u/%u events delivered", (unsigned)sent, (unsigned)v.size());
    v.clear();
}

static void SendBackspaces(int count)
{
    if (count <= 0) return;

    // Важно: отправлять ПАРЫ down+up со скан-кодом, а не серию down подряд.
    // Серии одинаковых keydown без keyup выглядят как автоповтор, и часть
    // приложений (Chromium и др.) обрабатывает их неполностью — слово
    // стирается не до конца. Скан-код обязателен: некоторые приложения
    // игнорируют события с пустым скан-кодом.
    UINT scan = MapVirtualKeyW(VK_BACK, 0 /*MAPVK_VK_TO_VSC*/);
    if (scan == 0) scan = 0x0E; // скан-код Backspace на всех PC-клавиатурах

    std::vector<INPUT> v;
    v.reserve((size_t)count * 2);
    for (int i = 0; i < count; ++i) {
        AddVk(v, VK_BACK, false);
        v.back().ki.wScan = (WORD)scan;
        AddVk(v, VK_BACK, true);
        v.back().ki.wScan = (WORD)scan;
    }
    SendAll(v);
}

// Перепечатать сохранённые нажатия скан-кодами (их интерпретирует уже
// новая раскладка потока). Возвращает число символов, которое они дали в hkl.
static int TypeKeys(const std::vector<KeyEntry>& keys, HKL hkl, bool send)
{
    // Симуляция количества символов через ToUnicodeEx
    BYTE state[256];
    ZeroMemory(state, sizeof(state));
    if (GetKeyState(VK_CAPITAL) & 1) state[VK_CAPITAL] = 0x01;

    int chars = 0;
    std::vector<INPUT> v;
    v.reserve(keys.size() * 8);

    for (size_t i = 0; i < keys.size(); ++i) {
        const KeyEntry& e = keys[i];

        // считаем символы в целевой раскладке
        BYTE st[256];
        memcpy(st, state, sizeof(st));
        if (e.shift) st[VK_SHIFT] = 0x80;
        if (e.altgr) { st[VK_CONTROL] = 0x80; st[VK_MENU] = 0x80; }
        wchar_t buf[8] = {0};
        int n = ToUnicodeEx(e.vk, e.scan, st, buf, 8, 0, hkl);
        if (n > 0) chars += n;
        memcpy(state, st, sizeof(state)); // «мёртвые» клавиши меняют состояние

        if (!send) continue;

        if (e.shift) AddVk(v, VK_SHIFT, false);
        if (e.altgr) { AddVk(v, VK_CONTROL, false); AddVk(v, VK_MENU, false); }

        AddScan(v, e.scan, false, e.extended);
        AddScan(v, e.scan, true,  e.extended);

        if (e.altgr) { AddVk(v, VK_MENU, true); AddVk(v, VK_CONTROL, true); }
        if (e.shift) AddVk(v, VK_SHIFT, true);
    }

    if (send) SendAll(v);
    return chars;
}

// Напечатать текст посимвольно (KEYEVENTF_UNICODE — с Windows 2000)
static void TypeUnicode(const std::wstring& text)
{
    std::vector<INPUT> v;
    v.reserve(text.size() * 2 + 2);
    for (size_t i = 0; i < text.size(); ++i) {
        wchar_t wc = text[i];
        // суррогатная пара — двумя символами в одном «нажатии»
        bool pair = (wc >= 0xD800 && wc <= 0xDBFF && i + 1 < text.size() &&
                     text[i + 1] >= 0xDC00 && text[i + 1] <= 0xDFFF);

        INPUT in;
        ZeroMemory(&in, sizeof(in));
        in.type = INPUT_KEYBOARD;
        in.ki.wScan = (WORD)wc;
        in.ki.dwFlags = KEYEVENTF_UNICODE;
        v.push_back(in);
        if (pair) {
            INPUT in2 = in;
            in2.ki.wScan = (WORD)text[i + 1];
            v.push_back(in2);
        }
        size_t added = pair ? 2 : 1;
        for (size_t k = 0; k < added; ++k) {
            INPUT up = v[v.size() - added + k];
            up.ki.dwFlags |= KEYEVENTF_KEYUP;
            v.push_back(up);
        }
        if (pair) ++i;
    }
    SendAll(v);
}

// ---------------------------------------------------------------------------
// Буфер обмена: полное сохранение и восстановление (все форматы)
// ---------------------------------------------------------------------------
struct ClipFmt { UINT fmt; HGLOBAL data; };

static bool SaveClipboard(std::vector<ClipFmt>& out)
{
    if (!OpenClipboard(NULL)) return false;
    UINT fmt = 0;
    while ((fmt = EnumClipboardFormats(fmt)) != 0) {
        HANDLE h = GetClipboardData(fmt);
        if (!h) continue;
        SIZE_T sz = GlobalSize(h);
        if (sz == 0) continue; // не HGLOBAL-формат — пропускаем
        void* p = GlobalLock(h);
        if (!p) continue;
        HGLOBAL dup = GlobalAlloc(GMEM_MOVEABLE, sz);
        if (dup) {
            void* dp = GlobalLock(dup);
            if (dp) {
                memcpy(dp, p, sz);
                GlobalUnlock(dup);
                ClipFmt cf; cf.fmt = fmt; cf.data = dup;
                out.push_back(cf);
            } else {
                GlobalFree(dup);
            }
        }
        GlobalUnlock(h);
    }
    CloseClipboard();
    return true;
}

static void RestoreClipboard(std::vector<ClipFmt>& saved)
{
    if (saved.empty()) return;
    if (OpenClipboard(NULL)) {
        EmptyClipboard();
        for (size_t i = 0; i < saved.size(); ++i) {
            HANDLE h = SetClipboardData(saved[i].fmt, saved[i].data);
            if (!h) GlobalFree(saved[i].data); // не удалось — освобождаем сами
        }
        CloseClipboard();
    } else {
        for (size_t i = 0; i < saved.size(); ++i) GlobalFree(saved[i].data);
    }
    saved.clear();
}

static bool GetClipText(std::wstring& out)
{
    if (!OpenClipboard(NULL)) return false;
    bool ok = false;
    HANDLE h = GetClipboardData(CF_UNICODETEXT);
    if (h) {
        void* p = GlobalLock(h);
        if (p) { out.assign((const wchar_t*)p); GlobalUnlock(h); ok = true; }
    } else {
        h = GetClipboardData(CF_TEXT);
        if (h) {
            void* p = GlobalLock(h);
            if (p) {
                int len = MultiByteToWideChar(CP_ACP, 0, (const char*)p, -1, NULL, 0);
                if (len > 0) {
                    out.resize((size_t)len - 1);
                    MultiByteToWideChar(CP_ACP, 0, (const char*)p, -1, &out[0], len);
                    ok = true;
                }
                GlobalUnlock(h);
            }
        }
    }
    CloseClipboard();
    return ok;
}

static void SendCopyCombo(UINT keyVk)
{
    std::vector<INPUT> v;
    AddVk(v, VK_CONTROL, false);
    AddVk(v, keyVk, false);
    AddVk(v, keyVk, true);
    AddVk(v, VK_CONTROL, true);
    SendAll(v);
}

// ---------------------------------------------------------------------------
// Рабочие потоки (вся последовательность выполняется вне потока хука)
// ---------------------------------------------------------------------------
static DWORD WINAPI ConvertWordThread(LPVOID param);
static DWORD WINAPI ToggleThread(LPVOID param);
static DWORD WINAPI SelectionOrSwitchThread(LPVOID param);

static void StartWorker(LPTHREAD_START_ROUTINE fn, LPVOID param)
{
    if (InterlockedExchange(&g_busy, 1) != 0) { // уже идёт конвертация
        delete (LastConversion*)param;
        return;
    }
    HANDLE h = CreateThread(NULL, 0, fn, param, 0, NULL);
    if (h) CloseHandle(h); // поток работает автономно
    else {
        InterlockedExchange(&g_busy, 0);
        delete (LastConversion*)param;
    }
}

// 1) Слово: стереть набранное и перепечатать те же клавиши в новой раскладке
static DWORD WINAPI ConvertWordThread(LPVOID param)
{
    LastConversion* snap = (LastConversion*)param; // здесь: keys = запись, src/dst пусты
    std::vector<KeyEntry> keys = snap->keys;
    delete snap;

    if (g_logEnabled) {
        // дамп записи: видно, все ли нажатия попали в запись и сколько
        // символов каждое породило
        std::string dump;
        char b[96];
        for (size_t i = 0; i < keys.size(); ++i) {
            _snprintf(b, sizeof(b), "vk=%u scan=%02X sh=%d ag=%d ch=%d | ",
                      keys[i].vk, keys[i].scan, keys[i].shift ? 1 : 0,
                      keys[i].altgr ? 1 : 0, keys[i].chars);
            dump += b;
        }
        LogF("ConvertWord record[%u]: %s", (unsigned)keys.size(), dump.c_str());
    }

    do {
        if (keys.empty()) break;

        HWND fg = GetForegroundWindow();
        if (!fg) break;
        DWORD tid = GetWindowThreadProcessId(fg, NULL);
        HKL src = GetKeyboardLayout(tid);
        HKL dst = NextLayoutInCycle(src);
        if (!dst || dst == src) break; // переключать некуда

        if (IsPasswordField()) { // страховка: в поле пароля только переключаем
            RequestLayoutSwitch(fg, dst, false);
            break;
        }

        WaitForModifiersUp(1000);

        int erase = 0;
        for (size_t i = 0; i < keys.size(); ++i) erase += keys[i].chars;

        LogF("ConvertWord: fg=%p tid=%lu keys=%u erase=%d src=%p dst=%p",
             (void*)fg, tid, (unsigned)keys.size(), erase, (void*)src, (void*)dst);

        SendBackspaces(erase);
        // Дать приложению обработать стирание до смены раскладки и перепечатки
        Sleep(30);
        bool switched = RequestLayoutSwitch(fg, dst, true);
        LogF("ConvertWord: layout switch confirmed=%d", switched ? 1 : 0);
        int typed = TypeKeys(keys, dst, true);
        LogF("ConvertWord: retyped chars=%d", typed);

        EnterCriticalSection(&g_stateCs);
        g_last.active   = true;
        g_last.reverted = false;
        g_last.keys     = keys;
        g_last.src      = src;
        g_last.dst      = dst;
        g_last.charsSrc = erase;
        g_last.charsDst = typed;
        LeaveCriticalSection(&g_stateCs);
    } while (false);

    InterlockedExchange(&g_busy, 0);
    return 0;
}

// 2) Переключение туда-обратно: возврат к исходному или повторная конвертация.
//    В snap->reverted лежит желаемое действие: true — вернуть исходное,
//    false — снова перепечатать в целевую раскладку.
static DWORD WINAPI ToggleThread(LPVOID param)
{
    LastConversion* lc = (LastConversion*)param;
    bool revert = lc->reverted;

    do {
        HWND fg = GetForegroundWindow();
        if (!fg) break;

        WaitForModifiersUp(1000);

        HKL to    = revert ? lc->src : lc->dst;
        int erase = revert ? lc->charsDst : lc->charsSrc;

        LogF("Toggle: revert=%d erase=%d keys=%u to=%p",
             revert ? 1 : 0, erase, (unsigned)lc->keys.size(), (void*)to);

        SendBackspaces(erase);
        Sleep(30); // дать приложению обработать стирание
        RequestLayoutSwitch(fg, to, true);
        int typed = TypeKeys(lc->keys, to, true);

        EnterCriticalSection(&g_stateCs);
        g_last.active = true;
        g_last.keys   = lc->keys;
        g_last.src    = lc->src;
        g_last.dst    = lc->dst;
        if (revert) {
            g_last.reverted = true;
            g_last.charsSrc = typed;   // уточнили, сколько символов в исходной
        } else {
            g_last.reverted = false;
            g_last.charsDst = typed;   // уточнили, сколько символов в целевой
        }
        LeaveCriticalSection(&g_stateCs);
    } while (false);

    InterlockedExchange(&g_busy, 0);
    delete lc;
    return 0;
}

// 3) Выделение или просто переключение
static DWORD WINAPI SelectionOrSwitchThread(LPVOID)
{
    do {
        HWND fg = GetForegroundWindow();
        if (!fg) break;
        DWORD tid = GetWindowThreadProcessId(fg, NULL);
        HKL cur = GetKeyboardLayout(tid);
        HKL dst = NextLayoutInCycle(cur);
        if (!dst || dst == cur) break;

        if (IsPasswordField()) { // в поле пароля — только переключение
            RequestLayoutSwitch(fg, dst, false);
            break;
        }

        WaitForModifiersUp(1000);

        // Пробуем скопировать выделение (Ctrl+C, запасной вариант Ctrl+Ins).
        // Если буфер не изменился — выделения нет.
        DWORD seq0 = GetClipboardSequenceNumber();
        std::vector<ClipFmt> saved;
        bool clipSaved = SaveClipboard(saved);

        std::wstring text;
        bool got = false;

        for (int attempt = 0; attempt < 2 && !got; ++attempt) {
            SendCopyCombo(attempt == 0 ? 'C' : VK_INSERT);
            for (int i = 0; i < 60; ++i) { // до ~600 мс
                Sleep(10);
                if (GetClipboardSequenceNumber() != seq0) {
                    if (GetClipText(text) && !text.empty()) got = true;
                    break;
                }
            }
            if (!got) {
                // сбросим возможный «нажатый» Ctrl перед следующей попыткой
                seq0 = GetClipboardSequenceNumber();
            }
        }

        if (!got) {
            if (clipSaved) RestoreClipboard(saved);
            LogF("Selection: no text on clipboard -> plain layout switch");
            RequestLayoutSwitch(fg, dst, false); // просто переключаем раскладку
            break;
        }

        std::wstring conv = ConvertText(text, cur, dst);
        LogF("Selection: copied %u chars, converted %u chars",
             (unsigned)text.size(), (unsigned)conv.size());
        if (clipSaved) RestoreClipboard(saved); // буфер — как был, все форматы

        if (conv == text) {
            RequestLayoutSwitch(fg, dst, false);
            break;
        }

        // Заменить выделение: Delete стирает выделенное, затем печатаем результат
        std::vector<INPUT> v;
        AddVk(v, VK_DELETE, false);
        AddVk(v, VK_DELETE, true);
        SendAll(v);
        Sleep(30);

        TypeUnicode(conv);
        RequestLayoutSwitch(fg, dst, false);
    } while (false);

    InterlockedExchange(&g_busy, 0);
    return 0;
}

// ---------------------------------------------------------------------------
// Точка входа по горячей клавише (вызывается из wndproc главного окна)
// ---------------------------------------------------------------------------
void DoTrigger()
{
    if (g_paused) return;

    // 1. Есть сохранённая конвертация и новых нажатий нет — туда/обратно
    EnterCriticalSection(&g_stateCs);
    LastConversion* snap = NULL;
    if (g_last.active) {
        snap = new LastConversion(g_last);
        snap->reverted = !g_last.reverted; // желаемое действие
    }
    LeaveCriticalSection(&g_stateCs);

    if (snap) {
        if (RecordSize() == 0) {
            StartWorker(ToggleThread, snap);
            return;
        }
        delete snap;
    }

    // 2. Есть записанные клавиши — перепечатать слово
    std::vector<KeyEntry> rec = TakeRecordSnapshot();
    if (!rec.empty()) {
        LastConversion* p = new LastConversion();
        p->keys = rec;
        StartWorker(ConvertWordThread, p);
        return;
    }

    // 3. Пусто — выделение или просто переключение
    StartWorker(SelectionOrSwitchThread, NULL);
}
