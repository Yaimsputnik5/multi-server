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
    close(app->clients[id].socket);
    app->clients[id].socket = -1;
}

void clientInputNew(App* app, int id)
{
    Client* c;
    int size;

    c = &app->clients[id];

    /* Read data */
    size = recv(c->socket, c->inBuf + c->inBufSize, 5 - c->inBufSize, 0);
    if (size == 0)
    {
        multiClientDisconnect(app, id);
        return;
    }
    if (size < 0)
        return;
    c->inBufSize += size;
    if (c->inBufSize < 5)
        return;

    /* We have read the data */
    if (memcmp(c->inBuf, "OoTMM", 5))
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
    c->state = CL_STATE_CONNECTED;
}

void multiClientInput(App* app, int id)
{
    app->clients[id].timeout = 0;

    switch (app->clients[id].state)
    {
    case CL_STATE_NEW:
        clientInputNew(app, id);
        break;
    }
}
