#include <Windows.h>
#include <cstdio>
#include <cassert>

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

    void timestampToIsoString(uint64_t timestamp, char* buffer, int len)
    {
        ULARGE_INTEGER time;
        time.QuadPart = timestamp * 10000;
        FILETIME time2 = { time.LowPart, time.HighPart };

        SYSTEMTIME time3;
        if (FileTimeToSystemTime(&time2, &time3) == TRUE)
        {
            sprintf_s(buffer, len, "%04d-%02d-%02dT%02d:%02d:%02d.%03dZ",
                time3.wYear, time3.wMonth, time3.wDay, time3.wHour, time3.wMinute, time3.wSecond, time3.wMilliseconds);
        }
        else {
            sprintf_s(buffer, len, "1970-01-01T00:00:00.000Z");
        }
    }

    bool isoStringToTimestamp(const char* timestamp, uint64_t* result)
    {
        // TODO: handle timezones other than UTC

        SYSTEMTIME time = {};
        sscanf_s(timestamp, "%4hd-%2hd-%2hdT%2hd:%2hd:%2hd.%3hd", &time.wYear, &time.wMonth, &time.wDay, &time.wHour, &time.wMinute, &time.wSecond, &time.wMilliseconds);

        FILETIME time2;
        if (SystemTimeToFileTime(&time, &time2))
        {
            if (result != nullptr)
            {
                ULARGE_INTEGER time3;
                time3.HighPart = time2.dwHighDateTime;
                time3.LowPart = time2.dwLowDateTime;
                *result = time3.QuadPart / 10000;
            }
            return true;
        }

        return false;
    }

    struct VirtualAllocHeader {
        size_t reservedByteCount;
        size_t committedByteCount;
        size_t sentinel;
    };

    void* virtualAlloc(size_t numBytes, size_t maxSize)
    {
        constexpr size_t headerSize = sizeof(VirtualAllocHeader);

        // convert byte counts into page counts
        const size_t commitByteCount = numBytes + headerSize;
        const size_t reserveByteCount = maxSize;

        if (commitByteCount > reserveByteCount) return nullptr;

        void* ptr = VirtualAlloc(nullptr, maxSize, MEM_RESERVE, PAGE_NOACCESS);
        if (ptr == nullptr) {
            return nullptr;
        }

        if (VirtualAlloc(ptr, commitByteCount, MEM_COMMIT, PAGE_READWRITE) == nullptr) {
            VirtualFree(ptr, 0, MEM_RELEASE);
            return nullptr;
        }

        VirtualAllocHeader* header = (VirtualAllocHeader*)ptr;
        header->reservedByteCount = reserveByteCount;
        header->committedByteCount = commitByteCount;
        header->sentinel = (size_t)-1;

        return (char*)ptr + headerSize;
    }

    bool virtualGrow(void* original, size_t newNumBytes)
    {
        VirtualAllocHeader* header = (VirtualAllocHeader*)((char*)original - sizeof(VirtualAllocHeader));
        assert(header->sentinel == (size_t)-1 && "Memory stomping detected! Something wrote past the beginning of the allocated space!");

        const size_t commitByteCount = newNumBytes + sizeof(VirtualAllocHeader);
        
        if (header->reservedByteCount < commitByteCount) {
            return false; // out of space
        }

        if (header->committedByteCount >= commitByteCount) {
            return true; // we've already committed enough memory
        }

        if (VirtualAlloc(header, newNumBytes, MEM_COMMIT, PAGE_READWRITE) != nullptr) {
            header->committedByteCount = commitByteCount;
            return true;
        }

        return false;
    }

    void virtualFree(void* original)
    {
        VirtualAllocHeader* header = (VirtualAllocHeader*)((char*)original - sizeof(VirtualAllocHeader));
        assert(header->sentinel == (size_t)-1 && "Memory stomping detected! Something wrote past the beginning of the allocated space!");

        VirtualFree(header, 0, MEM_RELEASE);
    }
}