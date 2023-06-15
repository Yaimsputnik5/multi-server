#ifndef APP_H
#define APP_H

#include <cstdint>

class App
{
public:
    App();
    ~App();

    int listen(const char* host, std::uint16_t port);
    int run();

private:
    int _socket;
};

#endif
