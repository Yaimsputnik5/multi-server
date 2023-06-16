#include <unistd.h>
#include <csdtring>
#include <MultiServer/Client.h>
#include <MultiServer/App.h>

Client::Client()
: Client{-1}
{
}

Client::Client(int socket)
: _socket{socket}
, _op{0}
, _inBufSize{0}
, _inBufTargetSize{0}
{
}

Client::~Client()
{
    if (_socket != -1)
        close(_socket);
}

Client::Client(Client&& other)
: _socket{other._socket}
, _op{other._op}
, _inBufSize{other._inBufSize}
, _inBufTargetSize{other._inBufTargetSize}
{
    other._socket = -1;
    std::memcpy(_inBuf, other._inBuf, _inBufSize);
}

Client& Client::operator=(Client&& other)
{
    if (_socket != -1)
        close(_socket);

    /* Move fd */
    _socket = other._socket;
    other._socket = -1;

    /* Move other data */
    _op = other._op;
    _inBufSize = other._inBufSize;
    _inBufTargetSize = other._inBufTargetSize;
    std::memcpy(_inBuf, other._inBuf, _inBufSize);

    return *this;
}

void Client::processInput(App& app, int id)
{
    /* Get the op */
}
