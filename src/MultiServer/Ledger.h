#ifndef MULTI_LEDGER_H
#define MULTI_LEDGER_H

#include <cstdint>

class Ledger
{
public:
    Ledger();
    ~Ledger();

    int open(const char* path);
    void add(std::uint64_t key, std::uint16_t size, const void* data);

private:

};

#endif
