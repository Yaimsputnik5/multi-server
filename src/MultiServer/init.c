#include "multi.h"

int multiInit(App* app)
{
    app->epoll = epoll_create1(0);
    app->socket = -1;
    app->timer = -1;
    app->clientSize = 0;
    app->clientCapacity = 8;
    app->clients = malloc(sizeof(Client) * app->clientCapacity);
    return 0;
}

int multiQuit(App* app)
{
    /* Close master socket */
    if (app->socket != -1)
        close(app->socket);

    /* Close client sockets */
    for (int i = 0; i < app->clientSize; ++i)
    {
        if (app->clients[i].socket != -1)
            close(app->clients[i].socket);
    }

    /* Close epoll */
    close(app->epoll);

    return 0;
}

int multiListen(App* app, const char* host, uint16_t port)
{
    struct epoll_event event;
    struct addrinfo hints;
    struct addrinfo* result;
    struct addrinfo* ptr;
    int ret;
    int s;
    char buffer[16];

    /* Setup the hints */
    bzero(&hints, sizeof(struct addrinfo));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE;

    /* Resolve the host */
    snprintf(buffer, sizeof(buffer), "%d", port);
    ret = getaddrinfo(host, buffer, &hints, &result);
    if (ret != 0)
    {
        fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(ret));
        return -1;
    }

    /* Try all results */
    s = -1;
    for (ptr = result; ptr != NULL; ptr = ptr->ai_next)
    {
        /* Create the socket */
        s = socket(ptr->ai_family, ptr->ai_socktype, ptr->ai_protocol);
        if (s == -1)
        {
            perror("socket");
            continue;
        }

        /* Set SO_REUSEADDR */
        ret = 1;
        ret = setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &ret, sizeof(ret));
        if (ret == -1)
        {
            perror("setsockopt");
            close(s);
            s = -1;
            continue;
        }

        /* Bind the socket */
        ret = bind(s, ptr->ai_addr, ptr->ai_addrlen);
        if (ret == -1)
        {
            close(s);
            s = -1;
            continue;
        }

        /* Listen on the socket */
        ret = listen(s, 5);
        if (ret == -1)
        {
            close(s);
            s = -1;
            continue;
        }

        /* We have a socket */
        break;
    }
    freeaddrinfo(result);
    if (s == -1)
    {
        fprintf(stderr, "Could not bind to %s:%d\n", host, port);
        return -1;
    }

    /* Make the socket non-blocking */
    ret = fcntl(s, F_SETFL, fcntl(s, F_GETFL, 0) | O_NONBLOCK);

    /* Add the socket to the epoll */
    event.events = EPOLLIN;
    event.data.u32 = APP_EP_SOCK_SERVER;
    ret = epoll_ctl(app->epoll, EPOLL_CTL_ADD, s, &event);
    if (ret == -1)
    {
        perror("epoll_ctl");
        close(s);
        return -1;
    }

    /* Save the socket */
    app->socket = s;
    printf("Listening on %s:%d\n", host, port);

    return 0;
}
