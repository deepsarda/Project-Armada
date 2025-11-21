#ifndef CLIENT_API_H
#define CLIENT_API_H

#include "../common/events.h"
#include "../common/game_types.h"

// Abstract interface for Client operations
typedef struct ClientContext ClientContext;

typedef struct
{
    // Core lifecycle
    int (*init)(ClientContext *ctx, const char *player_name);
    void (*connect)(ClientContext *ctx, const char *server_addr);
    void (*disconnect)(ClientContext *ctx);
    void (*pump)(ClientContext *ctx);

    // Event handling
    void (*send_action)(ClientContext *ctx, UserActionType action_type, int target_player_id, int value, int metadata);
    void (*handle_event)(ClientContext *ctx, const GameEvent *event);

    // Callbacks for UI/Logic
    void (*on_match_start)(ClientContext *ctx);
    void (*on_state_update)(ClientContext *ctx, const GameState *game);
    void (*on_game_over)(ClientContext *ctx, int winner_id);

} ClientInterface;

struct ClientContext
{
    int player_id;
    char player_name[32];
    ClientInterface *vtable;
    int connected;

    // Networking
    int socket_fd;
    PlayerGameState player_game_state;
    int has_state_snapshot;
};

// Factory method
ClientContext *client_create(const char *name);
void client_destroy(ClientContext *ctx);

#endif // CLIENT_API_H
