#ifndef MULTI_H
#define MULTI_H

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

typedef struct
{
    int socket;
    int state;
    int timeout;

    uint8_t     op;
    char        inBuf[256];
    uint32_t    inBufSize;
}
Client;

typedef struct
{
    int epoll;
    int socket;
    int timer;

    /* Clients */
    uint32_t clientSize;
    uint32_t clientCapacity;
    Client* clients;
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

#endif
