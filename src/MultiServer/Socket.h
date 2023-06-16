#ifndef MULTI_SERVER_SOCKET_H
#define MULTI_SERVER_SOCKET_H

class Socket
{
public:
    Socket();
    ~Socket();
    Socket(const Socket&) = delete;
    Socket& operator=(const Socket&) = delete;
    Socket(Socket&& other);
    Socket& operator=(Socket&& other);

    int fd() const { return _fd; }
    bool valid() const { return _fd != -1; }

private:
    int _fd;
};

#endif
