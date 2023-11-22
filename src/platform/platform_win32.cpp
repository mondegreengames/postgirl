#include <Windows.h>

namespace Platform
{
    uint64_t getUtcTimestampNow()
    {
        FILETIME time;
        GetSystemTimeAsFileTime(&time);

        ULARGE_INTEGER time2;
        time2.HighPart = time.dwHighDateTime;
        time2.LowPart = time.dwLowDateTime;

        return time2.QuadPart / 10000;
    }
}