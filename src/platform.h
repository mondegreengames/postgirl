#include <cstdint>

namespace Platform
{
    uint64_t getUtcTimestampNow();
    void timestampToIsoString(uint64_t timestamp, char* buffer, int len);
    bool isoStringToTimestamp(const char* timestamp, uint64_t* result);
}