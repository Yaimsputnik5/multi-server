#include <signal.h>
#include "multi.h"

static int usage(const char* prog)
{
    printf("Usage: %s [-h host] [-p port] [-d dataDir]\n", prog);
    return 2;
}

int main(int argc, char** argv)
{
    App app;
    const char* host;
    const char* dataDir;
    uint16_t port;
    int ret;

    /* Ignore SIGPIPE */
    signal(SIGPIPE, SIG_IGN);

    /* Parse options */
    host = "0.0.0.0";
    port = 13248;
    dataDir = "data";

    for (int i = 1; i < argc; ++i)
    {
        if (strcmp(argv[i], "-h") == 0)
        {
            i++;
            if (i >= argc)
                return usage(argv[0]);
            host = argv[i];
        }
        else if (strcmp(argv[i], "-p") == 0)
        {
            i++;
            if (i >= argc)
                return usage(argv[0]);
            port = atoi(argv[i]);
        }
        else if (strcmp(argv[i], "-d") == 0)
        {
            i++;
            if (i >= argc)
                return usage(argv[0]);
            dataDir = argv[i];
        }
        else
            return usage(argv[0]);
    }

    if (multiInit(&app, dataDir))
        return 1;
    if (multiListen(&app, host, port))
    {
        multiQuit(&app);
        return 1;
    }
    ret = multiRun(&app);
    if (multiQuit(&app))
        return 1;
    return ret;
}
