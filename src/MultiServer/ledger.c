#include <sys/stat.h>
#include "multi.h"

int multiLedgerOpen(App* app, const char* uuid)
{
    char bufBase[512];
    char buf[512];
    Ledger* l;
    int id;

    /* Find a previous ledger */
    for (int i = 0; i < app->ledgerSize; ++i)
    {
        l = app->ledgers + i;
        if (!l->valid)
            continue;
        if (memcmp(uuid, l->uuid, 16))
            continue;
        return i;
    }

    /* None exists - create one */
    if (app->ledgerSize == app->ledgerCapacity)
    {
        app->ledgerCapacity *= 2;
        app->ledgers = realloc(app->ledgers, sizeof(Ledger) * app->ledgerCapacity);
    }

    /* Create the ledger */
    id = app->ledgerSize++;
    l = app->ledgers + id;
    l->valid = 1;
    l->refCount = 1;
    memcpy(l->uuid, uuid, 16);

    /* Open ledger files */
    snprintf(bufBase, 512, "data/ledgers/%02x", uuid[0]);
    mkdir(bufBase, 0755);
    snprintf(bufBase, 512, "data/ledgers/%02x/%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x", uuid[0], uuid[1], uuid[2], uuid[3], uuid[4], uuid[5], uuid[6], uuid[7], uuid[8], uuid[9], uuid[10], uuid[11], uuid[12], uuid[13], uuid[14], uuid[15]);
    mkdir(bufBase, 0755);
    snprintf(buf, 512, "%s/data", bufBase);
    l->fileData = open(buf, O_APPEND | O_RDWR | O_CREAT, 0644);
    snprintf(buf, 512, "%s/index", bufBase);
    l->fileIndex = open(buf, O_APPEND | O_RDWR | O_CREAT, 0644);

    /* Extract entry count and ledger size */
    l->count = lseek(l->fileIndex, 0, SEEK_END) / 4;
    l->size = lseek(l->fileData, 0, SEEK_END);

    return id;
}

int paddingSize(int size)
{
    return (16 - (size % 16)) % 16;
}

static const char kZero[16] = { 0 };

static int fileWrite(int fd, const void* data, int size)
{
    int ret;
    int acc;

    while (size)
    {
        ret = write(fd, data, size);
        if (ret < 0)
            return -1;
        data = (const char*)data + ret;
        size -= ret;
    }

    return 0;
}

void multiLedgerWrite(App* app, int ledgerId, const void* data)
{
    Ledger* l;
    int padding;
    const LedgerEntryHeader* header;

    l = app->ledgers + ledgerId;

    /* Write the index */
    fileWrite(l->fileIndex, &l->size, 4);

    /* Write the data */
    header = (const LedgerEntryHeader*)data;
    fileWrite(l->fileData, data, header->size);
    padding = paddingSize(header->size);
    fileWrite(l->fileData, kZero, padding);

    /* Update ledger info */
    l->size += header->size + padding;
    l->count++;

    /* Sync */
    fsync(l->fileData);
    fsync(l->fileIndex);
}
