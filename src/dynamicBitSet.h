#pragma once
#include "pgvector.h"
#include <cstring>

class DynamicBitSet
{
    static constexpr int wordSizeInBits = sizeof(size_t) * 8;
public:
    pg::Vector<size_t> bits;

    bool isSet(unsigned int index) const
    {
        int wordIndex = index / wordSizeInBits;
        if (wordIndex >= bits.Size) return false;

        int bitIndex = index & (wordSizeInBits - 1);
        bool result = (bits[wordIndex] & ((size_t)1 << bitIndex)) != 0;

        return result;
    }

    void set(unsigned int index, bool value)
    {
        int wordIndex = index / wordSizeInBits;
        if (wordIndex > 1024 * 1024) return; // no, that's silly big

        if (wordIndex >= bits.Size) {
            int originalSize = bits.Size;
            bits.resize(wordIndex + 1);

            memset(bits.Data + originalSize, 0, (bits.Size - originalSize) * sizeof(size_t));
        }

        int bitIndex = index & (wordSizeInBits - 1);
        if (value)
            bits[wordIndex] |= (size_t)1 << bitIndex;
        else
            bits[wordIndex] &= ~((size_t)1 << bitIndex);
    }

    void setAll(bool value)
    {
        size_t value2 = value ? (size_t)-1 : 0;

        for (int i = 0; i < bits.Size; i++) {
            bits[i] = value2;
        }
    }

    int findFirstWithValue(bool value)
    {
        for (int i = 0; i < bits.Size; i++)
        {
            size_t bit = bits[i];

            if (!value)
                bit = ~bit;

            if (bit == 0) {
                continue;
            }
                
            static_assert(sizeof(size_t) == 4 || sizeof(size_t) == 8, "size_t must be either 4 or 8 bytes long");
            if (sizeof(size_t) == 4) {
#ifdef _WIN32
                unsigned long index;
                if (_BitScanForward(&index, (unsigned long)bit) == 0) {
                    index = 0;
                }
                int result = i * wordSizeInBits + index;
#else
                int result =  i * wordSizeInBits + __builtin_ffs(bit) - 1;
#endif
                assert(isSet(result) == value);
                return result;
            }
            if (sizeof(size_t) == 8) {
#ifdef _WIN32
                unsigned long index;
                if (_BitScanForward64(&index, bit) == 0) {
                    index = 0;
                }
                int result = i * wordSizeInBits + index;
#else
                int result = i * wordSizeInBits + __builtin_ffsl(bit) - 1;
#endif
                assert(isSet(result) == value);
                return result;
            }
        }

        return -1;
    }

    static void test()
    {
        DynamicBitSet s;
        s.set(10, true);
        assert(s.isSet(10) == true);
        s.set(10, false);
        s.set(68, true);
        s.set(69, true);
        assert(s.isSet(10) == false);
        assert(s.isSet(68) == true);
        assert(s.isSet(69) == true);
        assert(s.isSet(0) == false);
        assert(s.isSet(500) == false);
        assert(s.findFirstWithValue(false) == 0);
        assert(s.findFirstWithValue(true) == 68);

        s.setAll(true);
        assert(s.isSet(0) == true);
        assert(s.isSet(1) == true);
        assert(s.isSet(2) == true);
        assert(s.findFirstWithValue(false) == -1);
        assert(s.findFirstWithValue(true) == 0);
        s.set(5, false);
        assert(s.isSet(5) == false);
        assert(s.findFirstWithValue(false) == 5);
        assert(s.findFirstWithValue(true) == 0);
    }
};