#ifndef MULTI_H
#define MULTI_H

#define _XOPEN_SOURCE 500
#include <sys/epoll.h>
#include <unistd.h>
#include <string.h>
#include <strings.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <fcntl.h>
#include <netdb.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#define APP_EP_SOCK_SERVER  0x00000000
#define APP_EP_SOCK_CLIENT  0x01000000
#define APP_EP_TIMER        0x02000000
#define APP_EPTYPE(x)       ((x) & 0xff000000)
#define APP_EPVALUE(x)      ((x) & 0x00ffffff)

#define CL_STATE_NEW        0
#define CL_STATE_CONNECTED  1
#define CL_STATE_READY      2

#define OP_NONE             0
#define OP_TRANSFER         1

#define PACKED __attribute__((packed))

typedef struct
{
    uint64_t* data;
    uint32_t  size;
    uint32_t  capacity;
}
HashSet64;

void hashset64Init(HashSet64* set);
void hashset64Free(HashSet64* set);
void hashset64Add(HashSet64* set, uint64_t value);
int  hashset64Contains(HashSet64* set, uint64_t value);

typedef struct PAKCED
{
    uint64_t key;
    uint8_t  size;
}
LedgerEntryHeader;

typedef struct
{
    int         socket;
    int         state;
    int         timeout;
    int         ledgerId;
    uint32_t    ledgerBase;

    uint8_t     op;
    char        inBuf[256];
    uint32_t    inBufSize;

    char        outBuf[256];
    uint32_t    outBufSize;
    uint32_t    outBufPos;
}
Client;

typedef struct
{
    int     valid;
    char    uuid[16];
    int     refCount;

    int         fileData;
    int         fileIndex;
    uint32_t    count;
    uint32_t    size;

    HashSet64   keysSet;
}
Ledger;

typedef struct
{
    int epoll;
    int socket;
    int timer;

    /* Clients */
    uint32_t clientSize;
    uint32_t clientCapacity;
    Client* clients;

    /* Ledgers */
    uint32_t ledgerSize;
    uint32_t ledgerCapacity;
    Ledger* ledgers;
}
App;

int multiInit(App* app);
int multiQuit(App* app);
int multiListen(App* app, const char* host, uint16_t port);
int multiRun(App* app);

void multiClientNew(App* app, int s);
void multiClientDisconnect(App* app, int id);
void multiClientRemove(App* app, int id);
void multiClientInput(App* app, int id);
void multiClientNotify(App* app, int id);
void multiClientOutput(App* app, int id);

int  multiLedgerOpen(App* app, const char* uuid);
void multiLedgerWrite(App* app, int ledgerId, const void* data);

#endif
