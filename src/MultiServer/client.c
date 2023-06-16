#include "multi.h"

static int newClientId(App* app)
{
    /* Try to re-use a client ID */
    for (int i = 0; i < app->clientSize; ++i)
    {
        if (app->clients[i].socket == -1)
            return i;
    }

    /* Resize the client array if needed */
    if (app->clientSize == app->clientCapacity)
    {
        app->clientCapacity *= 2;
        app->clients = realloc(app->clients, sizeof(Client) * app->clientCapacity);
    }

    return app->clientSize++;
}

void multiClientNew(App* app, int sock)
{
    struct epoll_event event;
    int id;
    Client* client;

    /* Setup the new client */
    id = newClientId(app);
    client = &app->clients[id];
    client->socket = sock;
    client->state = CL_STATE_NEW;
    client->timeout = 0;
    client->ledgerId = -1;
    client->op = 0;
    client->inBufSize = 0;

    /* Add the client to epoll */
    event.events = EPOLLIN;
    event.data.u32 = APP_EP_SOCK_CLIENT | id;
    epoll_ctl(app->epoll, EPOLL_CTL_ADD, sock, &event);

    /* Log */
    printf("Client #%d: Connected\n", id);
}

void multiClientDisconnect(App* app, int id)
{
    printf("Client #%d: Disconnected\n", id);
    multiClientRemove(app, id);
}

void multiClientRemove(App* app, int id)
{
    int ledgerId;

    if (app->clients[id].socket == -1)
        return;
    ledgerId = app->clients[id].ledgerId;
    if (ledgerId != -1)
    {
        app->ledgers[ledgerId].refCount--;
    }
    close(app->clients[id].socket);
    app->clients[id].socket = -1;
}

static const void* clientRead(App* app, int id, int size)
{
    Client* c;
    int ret;

    c = &app->clients[id];

    if (c->inBufSize >= size)
        return c->inBuf;

    /* Read data */
    ret = recv(c->socket, c->inBuf + c->inBufSize, size - c->inBufSize, 0);
    if (ret == 0)
    {
        multiClientDisconnect(app, id);
        return NULL;
    }
    if (ret < 0)
        return NULL;
    c->inBufSize += ret;
    if (c->inBufSize < size)
        return NULL;

    /* We have read the data */
    return c->inBuf;
}

static void clientInputConnected(App* app, int id)
{
    Client* c;
    const void* data;
    uint32_t base;

    c = &app->clients[id];
    data = clientRead(app, id, 20);
    if (!data)
        return;

    memcpy(&base, (const char*)data + 16, 4);
    c->ledgerBase = base;
    c->ledgerId = multiLedgerOpen(app, data);

    if (app->ledgers[c->ledgerId].count < base)
    {
        printf("Client #%d: Invalid base %d\n", id, base);
        multiClientRemove(app, id);
        return;
    }

    /* Print */
    printf("Client #%d: Joined ledger %d\n", id, c->ledgerId);

    c->state = CL_STATE_READY;
}

static void clientInputNew(App* app, int id)
{
    Client* c;
    const void* data;

    c = &app->clients[id];
    data = clientRead(app, id, 5);
    if (!data)
        return;

    /* We have read the data */
    if (memcmp(data, "OoTMM", 5))
    {
        printf("Client #%d: Invalid header\n", id);
        multiClientRemove(app, id);
        return;
    }

    /* Reply */
    /* This is the first message, so we assume the send will succeed */
    send(c->socket, "OoTMM", 5, 0);

    /* Log */
    printf("Client #%d: Valid header\n", id);

    /* Set state and try to join */
    c->state = CL_STATE_CONNECTED;
    clientInputConnected(app, id);
}

static void clientInputTransfer(App* app, int id)
{
    Client* c;
    LedgerEntryHeader header;
    const void* data;

    c = &app->clients[id];
    data = clientRead(app, id, sizeof(header));
    if (!data)
        return;
    memcpy(&header, data, sizeof(header));
    if (header.size > 128)
    {
        printf("Client #%d: Invalid transfer size %d\n", id, header.size);
        multiClientRemove(app, id);
        return;
    }
    data = clientRead(app, id, sizeof(header) + header.size);
    if (!data)
        return;
    printf("Client #%d: Transfer %d bytes\n", id, header.size);
    multiLedgerWrite(app, c->ledgerId, data);
}

static void clientInputReady(App* app, int id)
{
    Client* c;
    const void* data;

    c = &app->clients[id];

    /* Read the operation */
    while (c->op == OP_NONE)
    {
        data = clientRead(app, id, 1);
        if (!data)
            return;
        c->op = *(const uint8_t*)data;
    }

    /* Trigger the operation */
    switch (c->op)
    {
    case OP_TRANSFER:
        clientInputTransfer(app, id);
        break;
    default:
        printf("Client #%d: Invalid operation %d\n", id, c->op);
        multiClientRemove(app, id);
        return;
    }
}

void multiClientInput(App* app, int id)
{
    app->clients[id].timeout = 0;

    switch (app->clients[id].state)
    {
    case CL_STATE_NEW:
        clientInputNew(app, id);
        break;
    case CL_STATE_CONNECTED:
        clientInputConnected(app, id);
        break;
    }
}
