#include <sys/stat.h>
#include "multi.h"

static void ledgerSetIndex(Ledger* l, uint32_t entryId, uint32_t idx)
{
    while (l->indexCapacity <= entryId)
    {
        l->indexCapacity *= 2;
        l->index = realloc(l->index, sizeof(uint32_t) * l->indexCapacity);
    }
    l->index[entryId] = idx;
}

static void makeLedger(App* app, const char* uuid, int id)
{
    Ledger* l;
    const uint8_t* u;
    char buf[512];
    char bufBase[512];

    l = app->ledgers + id;
    u = (const uint8_t*)uuid;

    /* Init the ledger */
    l->valid = 1;
    memcpy(l->uuid, uuid, 16);
    l->refCount = 0;
    l->indexCapacity = 512;
    l->index = malloc(sizeof(uint32_t) * l->indexCapacity);
    hashset64Init(&l->keysSet);

    /* Open ledger files */
    snprintf(bufBase, 512, "%s/ledgers/%02x", app->dataDir, u[0]);
    mkdir(bufBase, 0755);
    snprintf(bufBase, 512, "%s/ledgers/%02x/%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x", app->dataDir, u[0], u[1], u[2], u[3], u[4], u[5], u[6], u[7], u[8], u[9], u[10], u[11], u[12], u[13], u[14], u[15]);
    mkdir(bufBase, 0755);
    snprintf(buf, 512, "%s/data", bufBase);
    l->fileData = open(buf, O_APPEND | O_RDWR | O_CREAT, 0644);

    /* Extract entry count and ledger size */
    //l->count = lseek(l->fileIndex, 0, SEEK_END) / 4;
    //l->size = lseek(l->fileData, 0, SEEK_END);
    l->count = 0;
    l->size = 0;
}

int multiLedgerOpen(App* app, const char* uuid)
{
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
    makeLedger(app, uuid, id);
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
    header = (const LedgerEntryHeader*)data;

    /* Check for an existing key */
    if (hashset64Contains(&l->keysSet, header->key))
        return;

    /* Write the index */
    ledgerSetIndex(l, l->count, l->size);

    /* Write the data */
    fileWrite(l->fileData, data, sizeof(*header) + header->size);
    padding = paddingSize(sizeof(*header) + header->size);
    fileWrite(l->fileData, kZero, padding);

    /* Update ledger info */
    l->size += header->size + padding;
    l->count++;

    /* Sync */
    fsync(l->fileData);

    /* Add the key */
    hashset64Add(&l->keysSet, header->key);
}
