#include <cstdint>
#include <time.h>
#include <cstring>
#include <cstdio>

namespace Platform
{
    uint64_t convertTimestamp(time_t seconds, long nanoseconds, int milliseconds)
    {
        uint64_t result = seconds * 1000 + nanoseconds / 1000000 + milliseconds;
        return result;
    }

    uint64_t getUtcTimestampNow()
    {
        //static_assert(sizeof(time_t) <= 32, "time_t must be 32 bits or less");
        static_assert(sizeof(long) <= 32, "long must be 32 bits or less");
        timespec spec;
        if (clock_gettime(CLOCK_REALTIME, &spec) == 0)
        {
            uint64_t result = convertTimestamp(spec.tv_sec, spec.tv_nsec, 0);
            return result;
        }
        return 0;
    }

    void timestampToIsoString(uint64_t timestamp, char* buffer, int len)
    {
        time_t t = timestamp / 1000;

        tm datetime;
        gmtime_r(&t, &datetime);

        strftime(buffer, len, "%FT%T", &datetime);

        char ms[10];
        snprintf(ms, 10, ".%03ldZ", timestamp - timestamp / 1000 * 1000);
        strncat(buffer, ms, len);
    }

    bool isoStringToTimestamp(const char* timestamp, uint64_t* result)
    {
        if (timestamp == nullptr || result == nullptr) return false;

        tm t;
        int ms = 0;
        strptime(timestamp, "%Y-%m-%dT%H:%M:%S", &t);
        t.tm_isdst = -1;

        const char* dot = strchr(timestamp, '.');
        if (dot != nullptr)
            sscanf(dot + 1, "%d", &ms);

        time_t seconds = mktime(&t);
        *result = convertTimestamp(seconds, 0, ms);
        return true;
    }
}