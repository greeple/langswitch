// convert.cpp — таблицы раскладок (какая клавиша с какими модификаторами
// какой символ печатает) и посимвольная конвертация выделенного текста.
// Всё через ToUnicodeEx/MapVirtualKeyEx — доступны с Windows 2000.
#include "app.h"

#include <map>

#define MOD_ALTGR (MOD_CONTROL | MOD_ALT) // AltGr в Windows — это Ctrl+Alt

// Виртуальные клавиши, которые не печатают текст
static bool IsNonCharVk(UINT vk)
{
    if (vk >= 0x07 && vk <= 0x0F) return true;   // BACK..CLEAR (TAB не участвует)
    if (vk >= 0x10 && vk <= 0x1B) return true;   // SHIFT..ESCAPE
    if (vk >= 0x20 && vk <= 0x2F) return vk != VK_SPACE; // навигация, PRINT, SELECT
    if (vk >= 0x5B && vk <= 0x5F) return true;   // WIN, APPS
    if (vk >= 0x70 && vk <= 0x87) return true;   // F1..F24
    if (vk >= 0x90 && vk <= 0x91) return true;   // NUMLOCK, SCROLLLOCK
    if (vk == VK_SHIFT || vk == VK_CONTROL || vk == VK_MENU) return true;
    return false;
}

static std::map<HKL, LayoutMap> g_maps;
static CRITICAL_SECTION g_mapCs;
static bool g_mapCsInit = false;

static void EnsureMapCs()
{
    if (!g_mapCsInit) {
        InitializeCriticalSection(&g_mapCs);
        g_mapCsInit = true;
    }
}

static LayoutMap BuildLayoutMap(HKL hkl)
{
    LayoutMap m;
    static const DWORD modSets[4] = { 0, MOD_SHIFT, MOD_ALTGR, MOD_ALTGR | MOD_SHIFT };

    for (UINT vk = 0x08; vk <= 0xFE; ++vk) {
        if (IsNonCharVk(vk)) continue;
        UINT scan = MapVirtualKeyExW(vk, 0 /*MAPVK_VK_TO_VSC*/, hkl);
        // scan==0 бывает у «мёртвых» и служебных клавиш некоторых раскладок — пробуем всё равно

        for (int mi = 0; mi < 4; ++mi) {
            BYTE state[256];
            ZeroMemory(state, sizeof(state));
            if (modSets[mi] & MOD_SHIFT)    state[VK_SHIFT] = 0x80;
            if (modSets[mi] & MOD_CONTROL)  state[VK_CONTROL] = 0x80;
            if (modSets[mi] & MOD_ALT)      state[VK_MENU] = 0x80;

            wchar_t buf[8] = {0};
            int n = ToUnicodeEx((UINT)vk, scan, state, buf, 8, 0, hkl);
            if (n <= 0) continue;

            std::wstring s(buf, (size_t)n);
            // пропускаем управляющие символы
            bool ctrl = false;
            for (size_t i = 0; i < s.size(); ++i)
                if (s[i] < 0x20 && s[i] != L'\t') { ctrl = true; break; }
            if (ctrl) continue;

            std::pair<UINT, DWORD> key(vk, modSets[mi]);
            bool exists = false;
            for (size_t i = 0; i < m.keyToChar.size(); ++i)
                if (m.keyToChar[i].first == key) { exists = true; break; }
            if (!exists) m.keyToChar.push_back(std::make_pair(key, s));

            // в обратную карту — первое (простейшее) сочетание
            bool have = false;
            for (size_t i = 0; i < m.charToKey.size(); ++i)
                if (m.charToKey[i].first == s) { have = true; break; }
            if (!have) m.charToKey.push_back(std::make_pair(s, key));
        }
    }
    return m;
}

const LayoutMap& GetLayoutMap(HKL hkl)
{
    EnsureMapCs();
    EnterCriticalSection(&g_mapCs);
    std::map<HKL, LayoutMap>::iterator it = g_maps.find(hkl);
    if (it == g_maps.end())
        it = g_maps.insert(std::make_pair(hkl, BuildLayoutMap(hkl))).first;
    const LayoutMap& ref = it->second;
    LeaveCriticalSection(&g_mapCs);
    return ref;
}

static bool FindKeyForChar(const LayoutMap& m, wchar_t c, std::pair<UINT, DWORD>& out)
{
    wchar_t one[2] = { c, 0 };
    // точное совпадение одиночного символа
    for (size_t i = 0; i < m.charToKey.size(); ++i)
        if (m.charToKey[i].first == one) { out = m.charToKey[i].second; return true; }
    // символ может печататься «мёртвой» клавишей или лигатурой — ищем по первому символу строки
    for (size_t i = 0; i < m.charToKey.size(); ++i)
        if (!m.charToKey[i].first.empty() && m.charToKey[i].first[0] == c) {
            out = m.charToKey[i].second; return true;
        }
    return false;
}

static bool FindCharForKey(const LayoutMap& m, UINT vk, DWORD mods, std::wstring& out)
{
    std::pair<UINT, DWORD> key(vk, mods);
    for (size_t i = 0; i < m.keyToChar.size(); ++i)
        if (m.keyToChar[i].first == key) { out = m.keyToChar[i].second; return true; }
    return false;
}

// Если символа нет в целевой раскладке — пробуем «сместить регистр»:
// строчную версию символа ищем в источнике, печатаем с Shift в целевой (и наоборот).
static bool TryCaseFallback(const LayoutMap& src, const LayoutMap& dst,
                            wchar_t c, std::wstring& out)
{
    wchar_t other = iswupper(c) ? towlower(c) : towupper(c);
    if (other == c) return false;

    std::pair<UINT, DWORD> key;
    if (!FindKeyForChar(src, other, key)) return false;

    std::wstring s;
    DWORD flip = (key.second & MOD_SHIFT) ? (key.second & ~MOD_SHIFT) : (key.second | MOD_SHIFT);
    if (FindCharForKey(dst, key.first, flip, s)) { out = s; return true; }
    return false;
}

std::wstring ConvertText(const std::wstring& text, HKL src, HKL dst)
{
    const LayoutMap& msrc = GetLayoutMap(src);
    const LayoutMap& mdst = GetLayoutMap(dst);

    std::wstring out;
    out.reserve(text.size());

    for (size_t i = 0; i < text.size(); ++i) {
        wchar_t c = text[i];

        // управляющие и переводы строк — как есть
        if (c == L'\r' || c == L'\n' || c == L'\t') { out += c; continue; }

        // суррогатные пары — как есть (в таблицах раскладок их практически нет)
        if (c >= 0xD800 && c <= 0xDFFF) { out += c; continue; }

        std::pair<UINT, DWORD> key;
        std::wstring s;
        if (FindKeyForChar(msrc, c, key) && FindCharForKey(mdst, key.first, key.second, s)) {
            out += s;
            continue;
        }
        if (TryCaseFallback(msrc, mdst, c, s)) {
            out += s;
            continue;
        }
        // символа в раскладке-источнике нет (или нет в целевой) — переносим без изменения
        out += c;
    }
    return out;
}
