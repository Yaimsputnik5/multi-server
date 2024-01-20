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
#define BUFFER_SIZE 16384

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

typedef struct PACKED
{
    uint64_t key;
    uint8_t  size;
}
LedgerEntryHeader;

typedef struct
{
    char*       data;
    uint32_t    size;
    uint32_t    capacity;
    uint32_t    pos;
}
NetworkBuffer;

typedef struct
{
    int  id;
    int  valid;
    int  socket;
    int  state;

    uint32_t    version;

    int         ledgerId;
    uint32_t    ledgerBase;

    uint8_t     op;

    NetworkBuffer rx;
    NetworkBuffer tx;

    int rxTimeout;
    int txTimeout;
}
Client;

typedef struct
{
    int     valid;
    char    uuid[16];
    int     refCount;

    int         fileData;
    uint32_t    indexCapacity;
    uint32_t*   index;
    uint32_t    count;
    uint32_t    size;

    HashSet64   keysSet;
}
Ledger;

typedef struct
{
    int         epoll;
    int         socket;
    int         timer;
    int         error;
    const char* dataDir;

    /* Clients */
    int     clientSize;
    int     clientCapacity;
    Client* clients;

    /* Ledgers */
    int     ledgerSize;
    int     ledgerCapacity;
    Ledger* ledgers;
}
App;

int multiInit(App* app, const char* dataDir);
int multiQuit(App* app);
int multiListen(App* app, const char* host, uint16_t port);
int multiRun(App* app);

int  multiLedgerOpen(App* app, const char* uuid);
void multiLedgerWrite(App* app, int ledgerId, const void* data);
void multiLedgerClose(App* app, int ledgerId);

void multiFilePread(int fd, void* dst, uint32_t off, uint32_t size);

/* Client */
Client*     multiClientNew(App* app, int socket);
void        multiClientRemove(App* app, Client* client);
void        multiClientDisconnect(App* app, Client* client);
void        multiClientProcess(App* app, Client* client);
void        multiClientProcessNew(App* app, Client* client);
void        multiClientProcessConnected(App* app, Client* client);
void        multiClientProcessReady(App* app, Client* client);
void        multiClientCmdTransfer(App* app, Client* client);
void        multiClientEventTimer(App* app, Client* client);
void        multiClientEventInput(App* app, Client* client);
void        multiClientEventOutput(App* app, Client* client);
void        multiClientTransferLedger(App* app, Client* client);
int         multiClientPeek(App* app, Client* client, void* dst, uint32_t size);
int         multiClientRead(App* app, Client* client, void* dst, uint32_t size);
int         multiClientWrite(App* app, Client* client, const void* data, uint32_t size);
int         multiClientFlushIn(App* app, Client* client);
int         multiClientFlushOut(App* app, Client* client);


#endif
