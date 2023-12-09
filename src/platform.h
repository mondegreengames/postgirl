#pragma once

#include <cstdint>
#include <cstddef>

namespace Platform
{
    uint64_t getUtcTimestampNow();
    void timestampToIsoString(uint64_t timestamp, char* buffer, int len);
    bool isoStringToTimestamp(const char* timestamp, uint64_t* result);

    constexpr size_t DEFAULT_VIRTUAL_ALLOC_MAX_SIZE = 1024 * 1024 * 100;

    // reserves `maxSize` bytes and commits `numBytes` and returns a pointer to it. All memory is set to 0.
    // returns `nullptr` if unable to allocate
    void* virtualAlloc(size_t numBytes, size_t maxSize = DEFAULT_VIRTUAL_ALLOC_MAX_SIZE);

    // grows (commits) the memory up to the new size.
    // returns false if unable to grow
    bool virtualGrow(void* original, size_t newNumBytes);

    // completely frees the memory. `original` must be returned by `virtualAlloc()`
    void virtualFree(void* original);
}