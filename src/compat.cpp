// compat.cpp — совместимость с Windows 2000 / XP RTM.
//
// libgcc (gthr-win32.o) статически ссылается на GetThreadId, а эта функция
// появилась только в Windows XP SP1 / Server 2003 — на Windows 2000 и XP RTM
// EXE с таким импортом не запустился бы («точка входа не найдена»).
// Используется она libgcc исключительно для сравнения идентификаторов
// std::thread, которые эта программа не применяет. Подменяем ячейку таблицы
// импорта собственным определением: линкер не станет тянуть импорт из
// libkernel32.a, и бинарник остаётся пригодным для Windows 2000+.
#include <windows.h>

extern "C" {

static DWORD WINAPI GetThreadId_Fallback(HANDLE hThread)
{
    // Сюда выполнение не доходит: символ нужен только для удовлетворения
    // ссылки мертвого кода gthr-win32 (__gthr_win32_equal).
    (void)hThread;
    return 0;
}

// Точное имя символа, на который ссылается gthr-win32.o: __imp__GetThreadId@4
void* __imp__GetThreadId_slot asm("__imp__GetThreadId@4") =
    (void*)&GetThreadId_Fallback;

} // extern "C"
