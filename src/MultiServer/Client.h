#ifndef MULTI_CLIENT_H
#define MULTI_CLIENT_H

#include <cstdint>

class App;
class Client
{
public:
    Client();
    Client(int socket);
    ~Client();

    Client(const Client&) = delete;
    Client& operator=(const Client&) = delete;

    Client(Client&& other);
    Client& operator=(Client&& other);

    int socket() const { return _socket; }
    bool valid() const { return _socket != -1; }

    void processInput(App& app);

private:
    enum class State
    {
        New,
        NewConnected,
        Connected,
    };

    int             _socket;
    int             _state;
    std::uint8_t    _op;
    char            _inBuf[256];
    std::uint16_t   _inBufSize;
    std::uint16_t   _inBufTargetSize;
};

#endif
