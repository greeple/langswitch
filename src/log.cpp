// log.cpp — необязательная диагностика (debug_log=1 в config.ini).
// Пишет debug.log рядом с EXE (программа полностью портативная).
// Используется только для отладки; в обычной работе файл не создаётся.
#include "app.h"

#include <stdio.h>
#include <stdarg.h>

bool g_logEnabled = false;
static std::wstring g_logPath;
static CRITICAL_SECTION g_logCs;
static bool g_logCsInit = false;

static std::wstring LogDir()
{
    // тот же каталог, что и config.ini
    std::wstring p = SettingsPath();
    size_t pos = p.find_last_of(L'\\');
    if (pos != std::wstring::npos) p.erase(pos);
    return p;
}

void LogInit()
{
    if (!g_logCsInit) {
        InitializeCriticalSection(&g_logCs);
        g_logCsInit = true;
    }
    if (g_logEnabled) {
        g_logPath = LogDir() + L"\\debug.log";
        // свежая сессия — начинаем лог заново
        FILE* f = _wfopen(g_logPath.c_str(), L"wb");
        if (f) fclose(f);
    }
}

void LogF(const char* fmt, ...)
{
    if (!g_logEnabled) return;

    char body[1024];
    va_list ap;
    va_start(ap, fmt);
    _vsnprintf(body, sizeof(body) - 1, fmt, ap);
    va_end(ap);
    body[sizeof(body) - 1] = 0;

    SYSTEMTIME st;
    GetLocalTime(&st);
    char line[1280];
    _snprintf(line, sizeof(line) - 1, "[%02u:%02u:%02u.%03u] %s\r\n",
              st.wHour, st.wMinute, st.wSecond, st.wMilliseconds, body);
    line[sizeof(line) - 1] = 0;

    EnterCriticalSection(&g_logCs);
    FILE* f = _wfopen(g_logPath.c_str(), L"ab");
    if (f) {
        fputs(line, f);
        fclose(f);
    }
    LeaveCriticalSection(&g_logCs);
}
