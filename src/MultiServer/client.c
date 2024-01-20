#include <errno.h>
#include "multi.h"

static void bufferInit(NetworkBuffer* buf)
{
    buf->data = malloc(512);
    buf->size = 0;
    buf->capacity = 512;
    buf->pos = 0;
}

static void bufferFree(NetworkBuffer* buf)
{
    free(buf->data);
    buf->data = NULL;
}

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
    client->id = id;
    client->socket = sock;
    client->state = CL_STATE_NEW;
    client->timeout = 0;
    client->error = 0;
    client->version = 0;
    client->ledgerId = -1;
    client->op = 0;
    client->inBufSize = 0;
    bufferInit(&client->tx);
    client->txTimeout = 0;
    client->rxTimeout = 0;

    /* Configure epoll */
    event.events = EPOLLIN | EPOLLOUT | EPOLLET;
    event.data.u32 = APP_EP_SOCK_CLIENT | id;
    epoll_ctl(app->epoll, EPOLL_CTL_ADD, sock, &event);

    /* Log */
    fprintf(stderr, "Client #%d: Connected\n", id);
}

void multiClientDisconnect(App* app, int id)
{
    fprintf(stderr, "Client #%d: Disconnected\n", id);
    multiClientRemove(app, id);
}

void multiClientRemove(App* app, int id)
{
    int ledgerId;

    if (app->clients[id].socket == -1)
        return;
    ledgerId = app->clients[id].ledgerId;
    close(app->clients[id].socket);
    app->clients[id].socket = -1;

    bufferFree(&app->clients[id].tx);

    /* Un-ref the ledger */
    if (ledgerId != -1)
    {
        app->ledgers[ledgerId].refCount--;
        if (app->ledgers[ledgerId].refCount == 0)
            multiLedgerClose(app, ledgerId);
    }
}

static const void* clientRead(App* app, int id, uint32_t size)
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
    c->inBufSize = 0;
    memcpy(&base, (const char*)data + 16, 4);
    c->ledgerBase = base;
    c->ledgerId = multiLedgerOpen(app, data);

    if (app->ledgers[c->ledgerId].count < base)
    {
        fprintf(stderr, "Client #%d: Invalid base %d\n", id, base);
        multiClientRemove(app, id);
        return;
    }

    /* Print */
    fprintf(stderr, "Client #%d: Joined ledger %d\n", id, c->ledgerId);

    /* Set state and self-notify */
    c->state = CL_STATE_READY;
    multiClientTransferLedger(app, c);
}

static void clientInputNew(App* app, int id)
{
    Client* c;
    const void* data;
    uint32_t version;

    c = &app->clients[id];
    data = clientRead(app, id, 5);
    if (!data)
        return;

    if (memcmp(data, "OOMM2", 5) == 0)
    {
        /* New header */
        data = clientRead(app, id, 9);
        if (!data)
            return;
        memcpy(&version, (const char*)data + 5, 4);

    }
    else if (memcmp(data, "OoTMM", 5) == 0)
    {
        /* Old header */
        version = 0;
    }
    else
    {
        fprintf(stderr, "Client #%d: Invalid header\n", id);
        multiClientRemove(app, id);
        return;
    }

    /* Discard */
    c->inBufSize = 0;
    c->version = version;

    /* Reply */
    if (version)
        multiClientWrite(app, c, "OOMM2", 5);
    else
        multiClientWrite(app, c, "OoTMM", 5);

    /* Log */
    fprintf(stderr, "Client #%d: Valid header\n", id);

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
        fprintf(stderr, "Client #%d: Invalid transfer size %d\n", id, header.size);
        multiClientRemove(app, id);
        return;
    }
    data = clientRead(app, id, sizeof(header) + header.size);
    if (!data)
        return;
    c->inBufSize = 0;
    fprintf(stderr, "Client #%d: Transfer %d bytes\n", id, header.size);
    multiLedgerWrite(app, c->ledgerId, data);

    /* Set the op to NOP */
    c->op = OP_NONE;

    /* Notify all clients sharing the same ledger */
    for (int i = 0; i < app->clientSize; ++i)
    {
        if (app->clients[i].socket == -1)
            continue;
        if (app->clients[i].ledgerId != c->ledgerId)
            continue;
        multiClientTransferLedger(app, &app->clients[i]);
    }
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
        c->inBufSize = 0;
    }

    /* Trigger the operation */
    switch (c->op)
    {
    case OP_TRANSFER:
        clientInputTransfer(app, id);
        break;
    default:
        fprintf(stderr, "Client #%d: Invalid operation %d\n", id, c->op);
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
    case CL_STATE_READY:
        clientInputReady(app, id);
        break;
    }
}

static void* bufferReserve(NetworkBuffer* buf, uint32_t size)
{
    uint32_t newCapacity;
    char* newData;

    /* Check for space */
    if (buf->size + size > buf->capacity)
    {
        /* Compute the new capacity */
        do
        {
            newCapacity = buf->capacity * 2;
            if (newCapacity > BUFFER_SIZE)
                newCapacity = BUFFER_SIZE;

            /* If a max size buffer still doesn't fit, abort */
            if (newCapacity == buf->capacity)
                return NULL;
        }
        while (buf->size + size > newCapacity);

        /* Realloc */
        newData = realloc(buf->data, newCapacity);
        if (!newData)
            return NULL;
        buf->data = newData;
        buf->capacity = newCapacity;
    }

    /* We know we have enough space */
    return buf->data + buf->size;
}

/**
 * Called periodically to handle timeouts.
 */
void multiClientEventTimer(App* app, Client* client)
{
    char nop;

    if (client->error)
        return;

    /* Handle tx - send NOPs if we haven't sent anything in a while */
    client->txTimeout++;
    if (client->state == CL_STATE_READY && client->txTimeout > 3)
    {
        nop = OP_NONE;
        multiClientWrite(app, client, &nop, 1);
    }

    /* Handle rx */
    client->rxTimeout++;
    if (client->rxTimeout > 30)
    {
        fprintf(stderr, "Client #%d: Timeout\n", client->id);
        //multiClientRemove(app, client - app->clients);
    }
}

/**
 * Called every time a client is ready to send.
 */
void multiClientEventOutput(App* app, Client* client)
{
    if (client->error)
        return;

    /* First we need to flush the buffer */
    multiClientFlush(app, client);

    /* A ledger send might have been interrupted, resume if required */
    multiClientTransferLedger(app, client);
}

void multiClientTransferLedger(App* app, Client* client)
{
    Ledger* ledger;
    LedgerEntryHeader* header;
    uint32_t off;
    uint32_t size;
    char buffer[512];

    if (client->error)
        return;
    if (client->state != CL_STATE_READY)
        return;

    ledger = &app->ledgers[client->ledgerId];
    for (;;)
    {
        /* Check if we're at the end of the ledger */
        if (ledger->count <= client->ledgerBase)
            return;

        /* Prepare the payload */
        off = ledger->index[client->ledgerBase];
        buffer[0] = OP_TRANSFER;
        header = (LedgerEntryHeader*)(buffer + 1);
        multiFilePread(ledger->fileData, header, off, sizeof(*header));
        multiFilePread(ledger->fileData, buffer + 1 + sizeof(*header), off + sizeof(*header), header->size);
        size = 1 + sizeof(*header) + header->size;

        /* Send the entry */
        if (multiClientWrite(app, client, buffer, size))
            return;

        /* Entry is either sent or in the tx queue - either way, we're past it */
        client->ledgerBase++;
    }
}

/**
 * Send buffered data to the client.
 * @param client The client
 * @param data The data to send
 * @param size The size of the data
 * @return 0 on success, -1 on error
 */
int multiClientWrite(App* app, Client* client, const void* data, uint32_t size)
{
    void* dst;

    /* Check for errored client */
    if (client->error)
        return -1;

    /* Allocate */
    dst = bufferReserve(&client->tx, size);
    if (!dst)
        return -1;

    /* Copy */
    memcpy(dst, data, size);
    client->tx.size += size;

    /* Reset the tx timeout */
    client->txTimeout = 0;

    /* Trigger output */
    return multiClientFlush(app, client);
}

/**
 * Flush buffered data to the client.
 * @param client The client
 * @return 0 on success, -1 on error
 */
int multiClientFlush(App* app, Client* client)
{
    ssize_t ret;

    (void)app;

    if (client->error)
        return -1;

    for (;;)
    {
        /* Check for empty tx buffer */
        if (client->tx.pos == client->tx.size)
        {
            client->tx.pos = 0;
            client->tx.size = 0;
            return 0;
        }

        /* Send */
        ret = send(client->socket, client->tx.data + client->tx.pos, client->tx.size - client->tx.pos, 0);
        if (ret >= 0)
        {
            client->tx.pos += ret;
        }
        else
        {
            if (errno == EAGAIN || errno == EWOULDBLOCK)
                return 0;
            client->error = 1;
            return -1;
        }
    }
}
