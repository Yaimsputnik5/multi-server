#include <MultiServer/App.h>

int main(int argc, char** argv)
{
    App app;
    int ret;

    /* Listen on all interfaces on port 13248 */
    ret = app.listen("0.0.0.0", 13248);
    if (ret != 0)
        return 1;

    /* Run the application */
    return app.run();
}
