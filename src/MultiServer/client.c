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
        if (!app->clients[i].valid)
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

Client* multiClientNew(App* app, int sock)
{
    struct epoll_event event;
    int id;
    Client* client;

    /* Get a client */
    id = newClientId(app);
    client = &app->clients[id];
    memset(client, 0, sizeof(*client));

    /* Init */
    client->id = id;
    client->valid = 1;
    client->socket = sock;
    client->state = CL_STATE_NEW;
    client->ledgerId = -1;

    bufferInit(&client->rx);
    bufferInit(&client->tx);

    /* Configure epoll */
    event.events = EPOLLIN | EPOLLOUT | EPOLLET;
    event.data.u32 = APP_EP_SOCK_CLIENT | id;
    epoll_ctl(app->epoll, EPOLL_CTL_ADD, sock, &event);

    /* Log */
    fprintf(stderr, "Client #%d: Connected\n", id);

    /* Start processing */
    multiClientProcessNew(app, client);

    return client;
}

void multiClientDisconnect(App* app, Client* client)
{
    if (!client->valid)
        return;

    fprintf(stderr, "Client #%d: Disconnected\n", client->id);
    shutdown(client->socket, SHUT_RDWR);
    multiClientRemove(app, client);
}

void multiClientRemove(App* app, Client* client)
{
    int ledgerId;

    if (!client->valid)
        return;

    /* Destroy the client */
    client->valid = 0;
    ledgerId = client->ledgerId;
    close(client->socket);
    bufferFree(&client->rx);
    bufferFree(&client->tx);

    /* Un-ref the ledger */
    if (ledgerId != -1)
    {
        app->ledgers[ledgerId].refCount--;
        if (app->ledgers[ledgerId].refCount == 0)
            multiLedgerClose(app, ledgerId);
    }
}

void multiClientEventInput(App* app, Client* client)
{
    if (!client->valid)
        return;

    /* If there is an error, exit early */
    if (multiClientFlushIn(app, client))
        return;

    multiClientProcess(app, client);
}

void multiClientProcess(App* app, Client* client)
{
    if (!client->valid)
        return;

    switch (client->state)
    {
    case CL_STATE_NEW:
        multiClientProcessNew(app, client);
        break;
    case CL_STATE_CONNECTED:
        multiClientProcessConnected(app, client);
        break;
    case CL_STATE_READY:
        multiClientProcessReady(app, client);
        break;
    }
}

static void newClientReply(App* app, Client* client)
{
    uint32_t tmp32;
    uint16_t tmp16;
    char data[16];
    int size;

    if (!client->valid)
        return;

    if (!client->version)
    {
        memcpy(data, "OoTMM", 5);
        size = 5;
    }
    else
    {
        memcpy(data, "OOMM2", 5);
        tmp32 = VERSION;
        memcpy(data + 5, &tmp32, 4);
        tmp16 = (uint16_t)client->id;
        memcpy(data + 9, &tmp16, 2);
        size = 11;
    }

    multiClientWrite(app, client, data, size);
}

void multiClientProcessNew(App* app, Client* client)
{
    char buf[9];

    if (multiClientPeek(app, client, buf, 5))
        return;

    if (memcmp(buf, "OOMM2", 5) == 0)
    {
        /* New header */
        if (multiClientRead(app, client, buf, 9))
            return;
        memcpy(&client->version, buf + 5, 4);

    }
    else if (memcmp(buf, "OoTMM", 5) == 0)
    {
        /* Old header */
        if (multiClientRead(app, client, NULL, 5))
            return;
        client->version = 0;
    }
    else
    {
        fprintf(stderr, "Client #%d: Invalid header\n", client->id);
        multiClientRemove(app, client);
        return;
    }

    newClientReply(app, client);

    /* Log */
    fprintf(stderr, "Client #%d: Valid header\n", client->id);

    /* Set state and try to join */
    client->state = CL_STATE_CONNECTED;
    multiClientProcessConnected(app, client);
}

void multiClientProcessConnected(App* app, Client* client)
{
    char data[20];

    if (multiClientRead(app, client, data, 20))
        return;

    /* Copy the ledger base */
    memcpy(&client->ledgerBase, data + 16, 4);
    client->ledgerId = multiLedgerOpen(app, data);
    if (client->ledgerId == -1)
    {
        fprintf(stderr, "Client #%d: Invalid ledger\n", client->id);
        multiClientRemove(app, client);
        return;
    }

    if (app->ledgers[client->ledgerId].count < client->ledgerBase)
    {
        fprintf(stderr, "Client #%d: Invalid base %d\n", client->id, client->ledgerBase);
        multiClientRemove(app, client);
        return;
    }

    /* Print */
    fprintf(stderr, "Client #%d: Joined ledger %d\n", client->id, client->ledgerId);

    /* Set state */
    client->state = CL_STATE_READY;

    /* Transfer the ledger & run commands */
    multiClientTransferLedger(app, client);
    multiClientProcessReady(app, client);
}

void multiClientProcessReady(App* app, Client* client)
{
    if (!client->valid)
        return;

    /* Process commands */
    for (;;)
    {
        switch (client->op)
        {
        case OP_NONE:
            if (multiClientRead(app, client, &client->op, 1))
                return;
            break;
        case OP_TRANSFER:
            multiClientCmdTransfer(app, client);
            break;
        default:
            fprintf(stderr, "Client #%d: Invalid operation %d\n", client->id, client->op);
            multiClientRemove(app, client);
            return;
        }
    }
}

void multiClientCmdTransfer(App* app, Client* client)
{
    char data[256];
    LedgerEntryHeader header;

    if (!client->valid)
        return;

    if (multiClientPeek(app, client, &header, sizeof(header)))
        return;

    if (header.size > 128)
    {
        fprintf(stderr, "Client #%d: Invalid transfer size %d\n", client->id, header.size);
        multiClientRemove(app, client);
        return;
    }

    if (multiClientRead(app, client, data, sizeof(header) + header.size))
        return;

    fprintf(stderr, "Client #%d: Transfer %d bytes\n", client->id, header.size);
    multiLedgerWrite(app, client->ledgerId, data);

    /* Set the op to NOP */
    client->op = OP_NONE;

    /* Notify all clients sharing the same ledger */
    for (int i = 0; i < app->clientSize; ++i)
    {
        if (!app->clients[i].valid)
            continue;
        if (app->clients[i].ledgerId != client->ledgerId)
            continue;
        multiClientTransferLedger(app, &app->clients[i]);
    }
}

/* TODO: Use a ring buffer instead */
static void* bufferReserve(NetworkBuffer* buf, uint32_t size)
{
    uint32_t newCapacity;
    char* newData;

    /* First size check - move the stored data to the front */
    if (buf->size + size > buf->capacity && buf->pos)
    {
        memmove(buf->data, buf->data + buf->pos, buf->size - buf->pos);
        buf->size -= buf->pos;
        buf->pos = 0;
    }

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

    if (!client->valid)
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
    if (!client->valid)
        return;

    /* First we need to flush the buffer */
    multiClientFlushOut(app, client);

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

    if (!client->valid)
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

int multiClientPeek(App* app, Client* client, void* dst, uint32_t size)
{
    if (!client->valid)
        return -1;

    /* Do we need to read? */
    if (client->rx.size - client->rx.pos < size)
    {
        /* Read */
        if (multiClientFlushIn(app, client))
            return -1;

        /* Check again */
        if (client->rx.size - client->rx.pos < size)
            return -1;
    }

    if (dst)
        memcpy(dst, client->rx.data + client->rx.pos, size);
    return 0;
}

int multiClientRead(App* app, Client* client, void* dst, uint32_t size)
{
    if (multiClientPeek(app, client, dst, size))
        return -1;
    client->rx.pos += size;
    if (client->rx.pos == client->rx.size)
    {
        client->rx.pos = 0;
        client->rx.size = 0;
    }
    return 0;
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
    if (!client->valid)
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
    return multiClientFlushOut(app, client);
}

/**
 * Flush buffered data from the client.
 * @param client The client
 * @return 0 on success, -1 on error
 */
int multiClientFlushIn(App* app, Client* client)
{
    ssize_t ret;
    void* newData;
    uint32_t newCapacity;

    (void)app;

    if (!client->valid)
        return -1;

    for (;;)
    {
        if (client->rx.size == client->rx.capacity)
        {
            /* The buffer is full - expand if possible */
            if (client->rx.pos)
            {
                /* Expand by relocating */
                memmove(client->rx.data, client->rx.data + client->rx.pos, client->rx.size - client->rx.pos);
                client->rx.size -= client->rx.pos;
                client->rx.pos = 0;
            }
            else
            {
                /* Expand by realloc */
                newCapacity = client->rx.capacity * 2;
                if (newCapacity > BUFFER_SIZE)
                    newCapacity = BUFFER_SIZE;
                if (newCapacity == client->rx.capacity)
                {
                    /* Buffer is saturated */
                    return 0;
                }

                newData = realloc(client->rx.data, newCapacity);
                if (!newData)
                {
                    /* Out of memory, but not a read error */
                    return 0;
                }
                client->rx.data = newData;
                client->rx.capacity = newCapacity;
            }
        }

        ret = recv(client->socket, client->rx.data + client->rx.size, client->rx.capacity - client->rx.size, 0);
        if (ret == 0)
        {
            /* Sane disconnect */
            multiClientDisconnect(app, client);
            return -1;
        }
        else if (ret > 0)
        {
            client->rx.size += ret;
        }
        else
        {
            if (errno == EAGAIN || errno == EWOULDBLOCK)
            {
                client->rxTimeout = 0;
                return 0;
            }
            fprintf(stderr, "Client #%d: Read error %d\n", client->id, errno);
            multiClientRemove(app, client);
            return -1;
        }
    }
}

/**
 * Flush buffered data to the client.
 * @param client The client
 * @return 0 on success, -1 on error
 */
int multiClientFlushOut(App* app, Client* client)
{
    ssize_t ret;

    (void)app;

    if (!client->valid)
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
            fprintf(stderr, "Client #%d: Write error %d\n", client->id, errno);
            multiClientRemove(app, client);
            return -1;
        }
    }
}
