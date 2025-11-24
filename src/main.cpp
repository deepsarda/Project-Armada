#include "../include/client/tui_bridge.h"
#include <cstdio>

namespace
{
    struct DebugInit
    {
        DebugInit()
        {
            std::fprintf(stderr, "[armada] static init before main\n");
        }
    } debug_init;
}

int main()
{
    std::fprintf(stderr, "[armada] entering main\n");
    int rc = armada_tui_run();
    std::fprintf(stderr, "[armada] armada_tui_run returned %d\n", rc);
    return rc;
}
