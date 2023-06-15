#include <sys/types.h>
#include <sys/socket.h>
#include <fcntl.h>
#include <netdb.h>
#include <unistd.h>
#include <signal.h>
#include <cstdio>
#include <cerrno>
#include <MultiServer/App.h>

static sig_atomic_t sSignaled = 0;

static void sSigHandler(int signum)
{
    sSignaled = 1;
}

App::App()
: _socket{-1}
{
    _epoll = epoll_create1(0);
}

App::~App()
{
    /* Stop epoll */
    if (_epoll != -1)
    {
        close(_epoll);
        _epoll = -1;
    }

    /* Close the socket */
    if (_socket != -1)
    {
        close(_socket);
        _socket = -1;
    }
}

int App::listen(const char* host, std::uint16_t port)
{
    epoll_event event{};
    addrinfo hints{};
    addrinfo* result;
    addrinfo* ptr;
    char buffer[16];
    int ret;
    int s;

    /* Set up the hints structure */
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE;

    /* Set up the port string */
    std::snprintf(buffer, sizeof(buffer), "%hu", port);

    /* Get the address information */
    ret = getaddrinfo(host, buffer, &hints, &result);
    if (ret != 0)
    {
        std::fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(ret));
        return -1;
    }

    /* Try every result in sequence */
    s = -1;
    for (ptr = result; ptr != nullptr; ptr = ptr->ai_next)
    {
        /* Create the socket */
        s = socket(ptr->ai_family, ptr->ai_socktype, ptr->ai_protocol);
        if (s == -1)
            continue;

        /* Set the socket to non-blocking */
        ret = fcntl(s, F_SETFL, O_NONBLOCK);
        if (ret == -1)
        {
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

        /* We have a good socket */
        break;
    }
    freeaddrinfo(result);

    /* Check if we got a socket */
    if (s == -1)
    {
        std::fprintf(stderr, "Could not bind to %s:%hu\n", host, port);
        return -1;
    }

    /* Add the socket to epoll */
    event.events = EPOLLIN;
    event.data.fd = s;
    ret = epoll_ctl(_epoll, EPOLL_CTL_ADD, s, &event);
    if (ret == -1)
    {
        std::fprintf(stderr, "Could not add socket to epoll\n");
        close(s);
        return -1;
    }

    /* Listen on the socket */
    ret = ::listen(s, 5);
    if (ret == -1)
    {
        std::fprintf(stderr, "Could not listen on %s:%hu\n", host, port);
        close(s);
        return -1;
    }

    /* Display a message */
    std::printf("Listening on %s:%hu\n", host, port);

    /* Save the socket */
    _socket = s;
    return 0;
}

int App::run()
{
    epoll_event events[16];
    epoll_event* e;
    int eCount;
    int ret;

    /* Setup signals */
    signal(SIGINT, sSigHandler);
    signal(SIGTERM, sSigHandler);

    printf("Server started\n");

    /* Run the main loop */
    for (;;)
    {
        eCount = epoll_wait(_epoll, events, sizeof(events) / sizeof(events[0]), -1);

        if (eCount == -1)
        {
            /* Error handler */
            if (errno == EINTR)
            {
                /* Check if we were signaled */
                if (sSignaled)
                    break;
            }
            else
                break;
        }
    }

    printf("Closing server\n");

    return 0;
}
