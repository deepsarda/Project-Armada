#ifndef ARMADA_UI_NOTIFICATIONS_H
#define ARMADA_UI_NOTIFICATIONS_H

#include <stdarg.h>

#ifdef __cplusplus
extern "C"
{
#endif

    typedef void (*ArmadaUiLogSink)(const char *line, void *userdata);

    void armada_ui_set_log_sink(ArmadaUiLogSink sink, void *userdata);
    void armada_ui_log(const char *line);
    void armada_ui_logf(const char *fmt, ...);
    void armada_ui_vlogf(const char *fmt, va_list args);

#ifdef __cplusplus
}
#endif

#endif // ARMADA_UI_NOTIFICATIONS_H
