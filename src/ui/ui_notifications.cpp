#include "../../include/client/ui_notifications.h"

#include <cstddef>
#include <cstdarg>
#include <cstdio>
#include <mutex>

namespace
{
    std::mutex g_log_mutex;
    ArmadaUiLogSink g_log_sink = nullptr;
    void *g_log_userdata = nullptr;

    constexpr std::size_t kLogBufferSize = 512;
}

extern "C" void armada_ui_set_log_sink(ArmadaUiLogSink sink, void *userdata)
{
    std::lock_guard<std::mutex> lock(g_log_mutex);
    g_log_sink = sink;
    g_log_userdata = userdata;
}

extern "C" void armada_ui_log(const char *line)
{
    std::lock_guard<std::mutex> lock(g_log_mutex);
    if (!g_log_sink)
    {
        return;
    }
    g_log_sink(line ? line : "", g_log_userdata);
}

extern "C" void armada_ui_vlogf(const char *fmt, va_list args)
{
    if (!fmt)
    {
        return;
    }

    char buffer[kLogBufferSize];
    vsnprintf(buffer, sizeof(buffer), fmt, args);
    armada_ui_log(buffer);
}

extern "C" void armada_ui_logf(const char *fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    armada_ui_vlogf(fmt, args);
    va_end(args);
}
