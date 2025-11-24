#ifndef TUI_BRIDGE_H
#define TUI_BRIDGE_H

#include "../common/game_types.h"

#ifdef __cplusplus
extern "C"
{
#endif

    typedef enum
    {
        ARMADA_TUI_ACTION_NONE = 0,
        ARMADA_TUI_ACTION_HOST,
        ARMADA_TUI_ACTION_SCAN,
        ARMADA_TUI_ACTION_MANUAL_JOIN,
        ARMADA_TUI_ACTION_QUIT
    } ArmadaTuiAction;

#define ARMADA_DISCOVERY_MAX_RESULTS 32

    typedef struct
    {
        ArmadaTuiAction action;
        char manual_address[64];
        char player_name[MAX_NAME_LEN];
        int scan_timeout_ms;
        int scan_result_limit;
    } ArmadaTuiSelection;

    enum
    {
        ARMADA_TUI_STATUS_OK = 0,
        ARMADA_TUI_STATUS_EXIT_REQUESTED = 1,
        ARMADA_TUI_STATUS_UNAVAILABLE = -1,
        ARMADA_TUI_STATUS_ERROR = -2
    };

    int armada_tui_launch(ArmadaTuiSelection *selection);

#ifdef __cplusplus
}
#endif

#endif // TUI_BRIDGE_H
