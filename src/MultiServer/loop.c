#include <signal.h>
#include <errno.h>
#include "multi.h"

static sig_atomic_t sSignaled = 0;

static void signalHandler(int signum)
{
    (void)signum;
    sSignaled = 1;
}

static void handleNewClients(App* app)
{
    int s;

    for (;;)
    {
        /* Get the socket */
        s = accept(app->socket, NULL, NULL);
        if (s < 0)
            break;

        /* Make the socket non-blocking */
        fcntl(s, F_SETFL, fcntl(s, F_GETFL) | O_NONBLOCK);

        /* Add the client */
        multiClientNew(app, s);
    }
}

static void handleEvent(App* app, const struct epoll_event* e)
{
    switch (APP_EPTYPE(e->data.u32))
    {
    case APP_EP_SOCK_SERVER:
        if (e->events & EPOLLIN)
            handleNewClients(app);
        break;
    case APP_EP_SOCK_CLIENT:
        if (e->events & EPOLLIN)
            multiClientInput(app, APP_EPVALUE(e->data.u32));
        break;
    }
}

int multiRun(App* app)
{
    int eventCount;
    int ret;
    struct epoll_event events[16];

    /* Setup signal handlers */
    signal(SIGINT, signalHandler);
    signal(SIGTERM, signalHandler);

    ret = 0;
    for (;;)
    {
        eventCount = epoll_wait(app->epoll, events, 16, -1);
        if (sSignaled)
            break;
        if (eventCount < 0)
        {
            perror("epoll_wait");
            ret = 1;
            break;
        }

        for (int i = 0; i < eventCount; ++i)
            handleEvent(app, &events[i]);
    }

    /* Restore signal handlers */
    signal(SIGINT, SIG_DFL);
    signal(SIGTERM, SIG_DFL);

    printf("Shutting down\n");

    return ret;
}
