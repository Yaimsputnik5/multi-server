#include "multi.h"

static uint32_t hsHash(uint64_t value)
{
    uint32_t tmp;

    tmp = value ^ (value >> 32);
    tmp = ((tmp >> 16) ^ tmp) * 0x119de1f3;
    tmp = ((tmp >> 16) ^ tmp) * 0x119de1f3;
    tmp = (tmp >> 16) ^ tmp;

    return tmp;
}

static void hsInsert(uint64_t* table, uint32_t tableSize, uint64_t value)
{
    uint32_t h;
    uint32_t bucket;

    h = hsHash(value);
    bucket = h & (tableSize - 1);
    for (;;)
    {
        if (table[bucket] == 0)
        {
            table[bucket] = value;
            return;
        }
        bucket = (bucket + 1) & (tableSize - 1);
    }
}

static void hsRehash(HashSet64* set)
{
    uint64_t* newData;
    uint32_t newCapacity;

    newCapacity = set->capacity * 2;
    newData = malloc(sizeof(uint64_t) * newCapacity);
    memset(newData, 0, sizeof(uint64_t) * newCapacity);

    for (uint32_t i = 0; i < set->capacity; ++i)
    {
        if (set->data[i] != 0)
            hsInsert(newData, newCapacity, set->data[i]);
    }

    free(set->data);
    set->data = newData;
    set->capacity = newCapacity;
}

void hashset64Init(HashSet64* set)
{
    set->size = 0;
    set->capacity = 32;
    set->data = malloc(sizeof(uint64_t) * set->capacity);
    memset(set->data, 0, sizeof(uint64_t) * set->capacity);
}

void hashset64Free(HashSet64* set)
{
    free(set->data);
}

void hashset64Add(HashSet64* set, uint64_t value)
{
    if (set->size * 2 > set->capacity)
        hsRehash(set);

    hsInsert(set->data, set->capacity, value);
    set->size++;
}

int hashset64Contains(HashSet64* set, uint64_t value)
{
    uint32_t h;
    uint32_t bucket;
    uint64_t tmp;

    h = hsHash(value);
    bucket = h & (set->capacity - 1);
    for (;;)
    {
        tmp = set->data[bucket];
        if (tmp == 0)
            return 0;
        if (tmp == value)
            return 1;
        bucket = (bucket + 1) & (set->capacity - 1);
    }
}
