// settings.cpp — чтение/запись настроек в INI-файл.
// Программа полностью портативная: config.ini всегда лежит рядом с EXE.
// Только API уровня Windows 2000/XP.
#include "app.h"

static std::wstring ExeDir()
{
    wchar_t path[MAX_PATH] = {0};
    GetModuleFileNameW(g_hInst, path, MAX_PATH);
    std::wstring s(path);
    size_t p = s.find_last_of(L"\\/");
    if (p != std::wstring::npos) s.erase(p);
    return s;
}

std::wstring SettingsPath()
{
    return ExeDir() + L"\\config.ini";
}

static int IniInt(const std::wstring& path, const wchar_t* sec, const wchar_t* key, int def)
{
    return (int)GetPrivateProfileIntW(sec, key, def, path.c_str());
}

static std::wstring IniStr(const std::wstring& path, const wchar_t* sec, const wchar_t* key,
                           const wchar_t* def)
{
    wchar_t buf[2048] = {0};
    GetPrivateProfileStringW(sec, key, def, buf, 2048, path.c_str());
    return std::wstring(buf);
}

static void IniWrite(const std::wstring& path, const wchar_t* sec, const wchar_t* key,
                     const std::wstring& val)
{
    WritePrivateProfileStringW(sec, key, val.c_str(), path.c_str());
}

void LoadSettings()
{
    std::wstring path = SettingsPath();

    g_settings.autostart   = IniInt(path, L"general", L"autostart", 0) != 0;
    g_settings.startPaused = IniInt(path, L"general", L"start_paused", 0) != 0;
    g_settings.debugLog    = IniInt(path, L"general", L"debug_log", 0) != 0;
    g_logEnabled           = g_settings.debugLog;

    g_settings.hotkey.vk        = IniInt(path, L"hotkey", L"vk", VK_PAUSE);
    g_settings.hotkey.mods      = (DWORD)IniInt(path, L"hotkey", L"mods", 0);
    g_settings.hotkey.doubleTap = IniInt(path, L"hotkey", L"double_tap", 0) != 0;

    // Валидация горячей клавиши
    if (g_settings.hotkey.doubleTap) {
        g_settings.hotkey.vk = VK_SHIFT;
    } else if (g_settings.hotkey.vk <= 0 || g_settings.hotkey.vk > 254) {
        g_settings.hotkey.vk = VK_PAUSE;
        g_settings.hotkey.mods = 0;
    }

    // Цикл раскладок: "00000409,00000419"
    g_settings.cycle.clear();
    std::wstring cyc = IniStr(path, L"layouts", L"cycle", L"");
    size_t pos = 0;
    while (pos < cyc.size()) {
        size_t comma = cyc.find(L',', pos);
        std::wstring item = cyc.substr(pos, comma == std::wstring::npos ? std::wstring::npos : comma - pos);
        // убираем пробелы
        while (!item.empty() && (item[0] == L' ')) item.erase(0, 1);
        while (!item.empty() && (item[item.size() - 1] == L' ')) item.erase(item.size() - 1, 1);
        if (item.size() == 8) {
            HKL h = KlidToHkl(item);
            if (h) { // оставляем только реально установленные раскладки
                bool dup = false;
                for (size_t i = 0; i < g_settings.cycle.size(); ++i)
                    if (_wcsicmp(g_settings.cycle[i].c_str(), item.c_str()) == 0) { dup = true; break; }
                if (!dup) g_settings.cycle.push_back(item);
            }
        }
        if (comma == std::wstring::npos) break;
        pos = comma + 1;
    }
    if (g_settings.cycle.size() < 2) {
        // по умолчанию — все установленные раскладки в системном порядке
        g_settings.cycle.clear();
        std::vector<HKL> all = GetInstalledLayouts();
        for (size_t i = 0; i < all.size(); ++i)
            g_settings.cycle.push_back(HklToKlid(all[i]));
    }
}

void SaveSettings()
{
    std::wstring path = SettingsPath();

    wchar_t buf[64];

    IniWrite(path, L"general", L"autostart",   g_settings.autostart ? L"1" : L"0");
    IniWrite(path, L"general", L"start_paused", g_settings.startPaused ? L"1" : L"0");
    IniWrite(path, L"general", L"debug_log",   g_settings.debugLog ? L"1" : L"0");

    wsprintfW(buf, L"%d", g_settings.hotkey.vk);
    IniWrite(path, L"hotkey", L"vk", buf);
    wsprintfW(buf, L"%lu", (unsigned long)g_settings.hotkey.mods);
    IniWrite(path, L"hotkey", L"mods", buf);
    IniWrite(path, L"hotkey", L"double_tap", g_settings.hotkey.doubleTap ? L"1" : L"0");

    std::wstring cyc;
    for (size_t i = 0; i < g_settings.cycle.size(); ++i) {
        if (i) cyc += L",";
        cyc += g_settings.cycle[i];
    }
    IniWrite(path, L"layouts", L"cycle", cyc);

    // чтобы INI не кэшировался между процессами
    WritePrivateProfileStringW(NULL, NULL, NULL, path.c_str());
}
