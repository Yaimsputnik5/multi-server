#include "multi.h"

void multiFilePread(int fd, void* dst, uint32_t off, uint32_t size)
{
    ssize_t ret;

    while (size)
    {
        ret = pread(fd, dst, size, off);
        if (ret < 0)
            return;
        size -= ret;
        off += ret;
        dst = (char*)dst + ret;
    }
}
