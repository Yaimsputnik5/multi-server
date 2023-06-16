#include <signal.h>
#include "multi.h"

int main(int argc, char** argv)
{
    App app;
    int ret;

    /* Ignore SIGPIPE */
    signal(SIGPIPE, SIG_IGN);

    if (multiInit(&app))
        return 1;
    if (multiListen(&app, "0.0.0.0", 13248))
    {
        multiQuit(&app);
        return 1;
    }
    ret = multiRun(&app);
    if (multiQuit(&app))
        return 1;
    return ret;
}
