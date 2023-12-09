#include <cstdint>
#include <time.h>
#include <cstring>
#include <cstdio>
#include <sys/mman.h>
#include <unistd.h>
#include <cassert>

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

    struct VirtualAllocHeader {
        size_t reservedPageCount;
        size_t committedPageCount;
        size_t sentinel;
    };

    void* virtualAlloc(size_t numBytes, size_t maxSize)
    {   
        long sz = sysconf(_SC_PAGESIZE);

        constexpr size_t headerSize = sizeof(VirtualAllocHeader);
        
        // convert byte counts into page counts
        const size_t commitByteCount = numBytes + headerSize;
        const size_t commitPageCount = commitByteCount / sz + (commitByteCount % sz != 0);
        const size_t reservePageCount = maxSize / sz + (maxSize % sz != 0);

        if (commitPageCount > reservePageCount) return nullptr;

        void* ptr = mmap(nullptr, reservePageCount * sz, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        if (ptr == MAP_FAILED) {
            return nullptr;
        }

        // make sure what we committed will fit into physical memory, to avoid
        // innocent code like `*ptr = 3` causing out of memory errors
        mlock(ptr, reservePageCount * sz);

        // protect reserved-but-not-committed pages
        const int numUncommittedPages = reservePageCount - commitPageCount;
        if (numUncommittedPages) {
            mprotect((char*)ptr + commitPageCount * sz, numUncommittedPages * sz, PROT_NONE);
        }

        VirtualAllocHeader* header = (VirtualAllocHeader*)ptr;
        header->reservedPageCount = reservePageCount;
        header->committedPageCount = commitPageCount;
        header->sentinel = (size_t)-1;

        return (char*)ptr + headerSize;
    }

    bool virtualGrow(void* original, size_t newNumBytes)
    {
        VirtualAllocHeader* header = (VirtualAllocHeader*)((char*)original - sizeof(VirtualAllocHeader));
        assert(header->sentinel == (size_t)-1 && "Memory stomping detected! Something wrote past the beginning of the allocated space!");

        long sz = sysconf(_SC_PAGESIZE);

        const size_t commitByteCount = newNumBytes + sizeof(VirtualAllocHeader);
        const size_t commitPageCount = commitByteCount / sz + (commitByteCount % sz != 0);

        if (header->reservedPageCount < commitPageCount) {
            return false; // out of space
        }

        if (header->committedPageCount >= commitPageCount) {
            return true; // we've already committed enough memory
        }

        if (mprotect(header, commitPageCount * sz, PROT_READ | PROT_WRITE) != 0) {
            return false;
        }

        if (mlock(header, commitPageCount * sz) != 0) {
            return false;
        }

        header->committedPageCount = commitPageCount;

        return true;
    }

    void virtualFree(void* original)
    {
        VirtualAllocHeader* header = (VirtualAllocHeader*)((char*)original - sizeof(VirtualAllocHeader));
        assert(header->sentinel == (size_t)-1 && "Memory stomping detected! Something wrote past the beginning of the allocated space!");

        long sz = sysconf(_SC_PAGESIZE);
        munmap(original, header->reservedPageCount * sz);
    }
}