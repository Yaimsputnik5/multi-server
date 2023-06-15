#include <sys/types.h>
#include <sys/socket.h>
#include <fcntl.h>
#include <netdb.h>
#include <unistd.h>
#include <cstdio>
#include <MultiServer/App.h>

App::App()
: _socket{-1}
{
}

App::~App()
{
    /* Close the socket */
    if (_socket != -1)
    {
        close(_socket);
        _socket = -1;
    }
}

int App::listen(const char* host, std::uint16_t port)
{
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
    return 0;
}
