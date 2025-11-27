#ifndef CLIENT_API_H
#define CLIENT_API_H

#include "../common/events.h"
#include "../common/game_types.h"
#include "../networking/net_platform.h"

#ifdef __cplusplus
extern "C"
{
#endif

    typedef struct ClientContext
    {
        int player_id;
        char player_name[32];
        int connected;
        int host_player_id;
        int is_host;

        net_socket_t socket_fd;
        PlayerGameState player_game_state;
        int has_state_snapshot;

        // Match state tracking
        int match_started;
        int current_turn_player_id;
        int turn_number;
        int valid_actions; // Bitmask of valid actions for this player
    } ClientContext;

    ClientContext *client_create(const char *name);
    void client_destroy(ClientContext *ctx);

    int client_init(ClientContext *ctx, const char *player_name);
    int client_connect(ClientContext *ctx, const char *server_addr);
    void client_disconnect(ClientContext *ctx);
    void client_pump(ClientContext *ctx);
    void client_send_action(ClientContext *ctx, UserActionType action_type, int target_player_id, int value, int metadata);
    void client_request_match_start(ClientContext *ctx);

#ifdef __cplusplus
}
#endif

#endif // CLIENT_API_H
