#pragma once

#include "platform.h"


// This is similar to `pg::Vector`, except that pointers within the list are not invalidated by `push_back()`
template<typename T>
class DynamicArray {
public:
    size_t                      Size;
    size_t                      Capacity;
    T*                          Data;

    typedef T                   value_type;
    typedef value_type*         iterator;
    typedef const value_type*   const_iterator;


    DynamicArray()
        : Size(0), Capacity(0), Data(nullptr)
    {
    }

    ~DynamicArray()
    {
        if (Data != nullptr) {
            Platform::virtualFree(Data);
            Data = nullptr;
        }

        Size = Capacity = 0;
    }

    inline iterator             begin()                         { return Data; }
    inline const_iterator       begin() const                   { return Data; }
    inline iterator             end()                           { return Data + Size; }
    inline const_iterator       end() const                     { return Data + Size; }
    inline value_type&          front()                         { assert(Size > 0); return Data[0]; }
    inline const value_type&    front() const                   { assert(Size > 0); return Data[0]; }
    inline value_type&          back()                          { assert(Size > 0); return Data[Size - 1]; }
    inline const value_type&    back() const                    { assert(Size > 0); return Data[Size - 1]; }

    inline void         resize(size_t new_size)                    { if (new_size > Capacity) reserve(_grow_capacity(new_size)); Size = new_size; }
    inline void         resize(size_t new_size,const value_type& v){ if (new_size > Capacity) reserve(_grow_capacity(new_size)); if (new_size > Size) for (int n = Size; n < new_size; n++) Data[n] = v; Size = new_size; }
    inline void         reserve(size_t new_capacity)
    {
        if (new_capacity <= Capacity)
            return;

        if (Data == nullptr) {
            Data = (T*)Platform::virtualAlloc(new_capacity * sizeof(T));
            Capacity = new_capacity;
            return;
        }

        Platform::virtualGrow(Data, new_capacity * sizeof(T));

        Capacity = new_capacity;
    }

    // NB: It is forbidden to call push_back/push_front/insert with a reference pointing inside the Vector data itself! e.g. v.push_back(v[10]) is forbidden.
    inline size_t       push_back(const value_type& v)                  { if (Size == Capacity) reserve(_grow_capacity(Size + 1)); Data[Size] = v; return Size++;}
    inline void         pop_back()                                      { assert(Size > 0); Size--; }
    inline iterator     erase(const_iterator it)                        { assert(it >= Data && it < Data+Size); const ptrdiff_t off = it - Data; memmove(Data + off, Data + off + 1, ((size_t)Size - (size_t)off - 1) * sizeof(value_type)); Size--; return Data + off; }
    inline iterator     erase(const_iterator it, const_iterator it_last){ assert(it >= Data && it < Data+Size && it_last > it && it_last <= Data+Size); const ptrdiff_t count = it_last - it; const ptrdiff_t off = it - Data; memmove(Data + off, Data + off + count, ((size_t)Size - (size_t)off - count) * sizeof(value_type)); Size -= (int)count; return Data + off; }
    inline T*           insert(const T* it, const T& v)                 { assert(it >= Data && it <= Data + Size); const ptrdiff_t off = it - Data; if (Size == Capacity) reserve(_grow_capacity(Size + 1)); if (off < (int)Size) memmove(Data + off + 1, Data + off, ((size_t)Size - (size_t)off) * sizeof(T)); memcpy(&Data[off], &v, sizeof(v)); Size++; return Data + off; }
    inline bool         contains(const value_type& v) const             { const T* data = Data;  const T* data_end = Data + Size; while (data < data_end) if (*data++ == v) return true; return false; }


private:
    inline size_t       _grow_capacity(size_t sz) const            { size_t new_capacity = Capacity ? (Capacity + Capacity/2) : 8; return new_capacity > sz ? new_capacity : sz; }
};

template<typename T>
class DynamicDeletableArray {
public:
    class DynamicDeletableArrayIterator
    {
        DynamicDeletableArray<T>& parent;
        size_t index;

    public:

        DynamicDeletableArrayIterator(DynamicDeletableArray<T>& parent)
            : parent(parent), index(0)
        {}

        T* getPtr() {
            assert(index < parent.data.Size);
            assert(parent.alive.isSet(index) == true);

            return &parent.data.Data[index];
        }
        
        T& get() {
            assert(index < parent.data.Size);
            assert(parent.alive.isSet(index) == true);

            return parent.data.Data[index];
        }

        bool next() {
            index++;
            while(index < parent.data.Size && parent.alive.isSet(index) == false) {
                index++;
            }

            return index < parent.data.Size && parent.alive.isSet(index);
        }
    };

    DynamicArray<T> data;
    DynamicBitSet alive;

    inline size_t push_back(const T& v) {
        size_t index = data.push_back(v);
        alive.set(index, true);
        return index;
    }

    inline void pop_back() {
        if (data.Size > 0) {
            alive.set(data.Size - 1, false);
            data.pop_back();
        }
    }

    inline size_t insert(const T& v) {
        int index = alive.findFirstWithValue(false);
        if (index >= 0 && (unsigned int)index < data.Size) {
            alive.set(index, true);
            data.Data[index] = v;
            return index;
        }

        return push_back(v);
    }

    inline bool erase(size_t index) {
        if (index < data.Size) {
            alive.set(index, false);
            return true;
        }

        return false;
    }

    inline bool tryGet(size_t index, T& result) {
        if (alive.isSet(index) && index < data.Size) {
            result = data.Data[index];
            return true;
        }

        return false;
    }

    inline T* tryGetPtr(size_t index) {
        if (alive.isSet(index) && index < data.Size) {
            return &data.Data[index];
        }

        return nullptr;
    }
};