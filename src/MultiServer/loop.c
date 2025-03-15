#include <signal.h>
#include <errno.h>
#include <sys/timerfd.h>
#include <netinet/tcp.h>
#include <time.h>
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
    int one;

    for (;;)
    {
        /* Get the socket */
        s = accept(app->socket, NULL, NULL);
        if (s < 0)
            break;

        /* Set TCP_NODELAY */
        one = 1;
        setsockopt(s, SOL_TCP, TCP_NODELAY, &one, sizeof(one));

        /* Make the socket non-blocking */
        fcntl(s, F_SETFL, fcntl(s, F_GETFL) | O_NONBLOCK);

        /* Add the client */
        multiClientNew(app, s);
    }
}

static void handleTimer(App* app)
{
    uint64_t value;

    /* Read the timer */
    read(app->timer, &value, sizeof(value));

    /* Handle the timer */
    for (int i = 0; i < app->clientSize; ++i)
        multiClientEventTimer(app, &app->clients[i]);
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
        if (e->events & EPOLLHUP)
        {
            multiClientDisconnect(app, &app->clients[APP_EPVALUE(e->data.u32)]);
            break;
        }
        if (e->events & EPOLLIN)
            multiClientEventInput(app, &app->clients[APP_EPVALUE(e->data.u32)]);
        if (e->events & EPOLLOUT)
            multiClientEventOutput(app, &app->clients[APP_EPVALUE(e->data.u32)]);
        break;
    case APP_EP_TIMER:
        handleTimer(app);
        break;
    }
}

static void runSetup(App* app)
{
    struct itimerspec itsp;
    struct epoll_event event;

    /* Setup signal handlers */
    signal(SIGINT, signalHandler);
    signal(SIGTERM, signalHandler);

    /* Setup the timer */
    app->timer = timerfd_create(CLOCK_MONOTONIC, 0);
    if (app->timer == -1)
    {
        perror("timerfd_create");
        exit(1);
    }

    memset(&itsp, 0, sizeof(itsp));
    itsp.it_value.tv_sec = 1;
    itsp.it_interval.tv_sec = 1;
    timerfd_settime(app->timer, 0, &itsp, NULL);

    memset(&event, 0, sizeof(event));
    event.events = EPOLLIN;
    event.data.u32 = APP_EP_TIMER;
    epoll_ctl(app->epoll, EPOLL_CTL_ADD, app->timer, &event);
}

int multiRun(App* app)
{
    int eventCount;
    int ret;
    struct epoll_event events[16];

    /* Setup */
    runSetup(app);

    ret = 0;
    for (;;)
    {
        //printf("WAIT\n");
        eventCount = epoll_wait(app->epoll, events, 16, -1);
        //printf("WAIT END %d\n", eventCount);
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

    fprintf(stderr, "Server: Shutting down\n");

    return ret;
}
