// layouts.cpp — перечисление раскладок, KLID<->HKL, имена, переключение цикла.
// Переключение раскладки чужого потока — через WM_INPUTLANGCHANGEREQUEST
// (единственный способ, работающий в Windows 2000/XP, где раскладка per-thread).
#include "app.h"

std::vector<HKL> GetInstalledLayouts()
{
    std::vector<HKL> out;
    int n = GetKeyboardLayoutList(0, NULL);
    if (n <= 0) return out;
    out.resize((size_t)n);
    n = GetKeyboardLayoutList(n, &out[0]);
    if (n <= 0) out.clear();
    else out.resize((size_t)n);
    return out;
}

std::wstring HklToKlid(HKL hkl)
{
    // GetKeyboardLayoutNameW возвращает имя только для раскладки ТЕКУЩЕГО потока,
    // поэтому временно активируем нужную в своём потоке и возвращаем обратно.
    HKL prev = GetKeyboardLayout(0);
    wchar_t name[KL_NAMELENGTH + 1] = {0};
    if (ActivateKeyboardLayout(hkl, 0) != NULL) {
        GetKeyboardLayoutNameW(name);
        ActivateKeyboardLayout(prev, 0);
    }
    if (name[0] == 0) {
        // запасной вариант: собрать KLID из самого HKL
        unsigned long v = (unsigned long)(ULONG_PTR)hkl;
        wsprintfW(name, L"%08lX", v);
    }
    return std::wstring(name);
}

HKL KlidToHkl(const std::wstring& klid)
{
    // флаг 0: загрузить (для уже установленных — просто вернуть HKL), не активируя
    return LoadKeyboardLayoutW(klid.c_str(), 0);
}

std::wstring LayoutDisplayName(HKL hkl)
{
    // Нижний слово HKL — языковой идентификатор раскладки
    LANGID lang = (LANGID)(ULONG_PTR)hkl;
    wchar_t name[128] = {0};
    int n = GetLocaleInfoW(MAKELCID(lang, SORT_DEFAULT), LOCALE_SNATIVELANGUAGENAME, name, 128);
    std::wstring out;
    if (n > 0) out = name;
    else {
        wchar_t buf[16];
        wsprintfW(buf, L"%04X", (unsigned)lang);
        out = buf;
    }

    // Вариант раскладки: старшее слово HKL отличается от младшего (например Dvorak, машинопись)
    WORD lo = (WORD)(ULONG_PTR)hkl;
    WORD hi = (WORD)((ULONG_PTR)hkl >> 16);
    if (hi != lo && hi != 0) {
        wchar_t buf[16];
        wsprintfW(buf, L" (%04X)", (unsigned)hi);
        out += buf;
    }
    return out;
}

HKL NextLayoutInCycle(HKL current)
{
    if (g_settings.cycle.size() < 2) return current;

    // HKL текущей раскладки может отличаться от загруженного по KLID —
    // сравниваем через KLID-строки.
    std::wstring curKlid = HklToKlid(current);
    int idx = -1;
    for (size_t i = 0; i < g_settings.cycle.size(); ++i) {
        if (_wcsicmp(g_settings.cycle[i].c_str(), curKlid.c_str()) == 0) { idx = (int)i; break; }
    }
    if (idx < 0) idx = -1; // текущая не в цикле — начнём с первой
    size_t next = (size_t)(idx + 1) % g_settings.cycle.size();
    HKL h = KlidToHkl(g_settings.cycle[next]);
    return h ? h : current;
}

bool RequestLayoutSwitch(HWND fgWnd, HKL target, bool wait)
{
    if (!fgWnd || !target) return false;
    DWORD tid = GetWindowThreadProcessId(fgWnd, NULL);
    if (!tid) return false;

    // lParam = HKL: просим именно эту раскладку (Windows 2000/XP)
    PostMessageW(fgWnd, WM_INPUTLANGCHANGEREQUEST, 0, (LPARAM)target);

    if (!wait) return true;

    // Ждём, пока поток переднего плана действительно её применит (до ~0,5 с)
    for (int i = 0; i < 50; ++i) {
        if (GetKeyboardLayout(tid) == target) return true;
        Sleep(10);
    }

    // Некоторые приложения не обрабатывают сообщение в окне — пробуем
    // послать его напрямую потоку и ждём ещё ~0,5 с.
    PostThreadMessageW(tid, WM_INPUTLANGCHANGEREQUEST, 0, (LPARAM)target);
    for (int i = 0; i < 50; ++i) {
        if (GetKeyboardLayout(tid) == target) return true;
        Sleep(10);
    }
    return GetKeyboardLayout(tid) == target;
}
