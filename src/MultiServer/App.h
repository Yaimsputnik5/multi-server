#ifndef APP_H
#define APP_H

#include <cstdint>
#include <sys/epoll.h>
#include <vector>
#include <MultiServer/Client.h>

class App
{
public:
    App();
    ~App();

    int listen(const char* host, std::uint16_t port);
    int run();

private:
    void acceptClients();

    int                 _epoll;
    int                 _socket;
    std::vector<Client> _clients;
    std::vector<int>    _freeListClientIds;
};

#endif
