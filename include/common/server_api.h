#ifndef SERVER_API_H
#define SERVER_API_H

#include <pthread.h>

#include "../common/events.h"
#include "../common/game_types.h"

// Abstract interface for Server operations
typedef struct ServerContext ServerContext;

typedef struct
{
    // Core lifecycle
    int (*init)(ServerContext *ctx, int max_players);
    void (*start)(ServerContext *ctx);
    void (*stop)(ServerContext *ctx);

    // Event handling
    void (*broadcast_event)(ServerContext *ctx, const GameEvent *event);
    void (*send_event_to)(ServerContext *ctx, int player_id, const GameEvent *event);
    void (*process_event)(ServerContext *ctx, const GameEvent *event);

    // Game logic hooks
    void (*on_player_join)(ServerContext *ctx, int sender_id, const EventPayload_PlayerJoin *payload);
    void (*on_user_action)(ServerContext *ctx, const EventPayload_UserAction *payload);

} ServerInterface;

struct ServerContext
{
    GameState game_state;
    ServerInterface *vtable; // Pointer to the interface implementation
    int running;
    int max_players;

    // Networking
    int server_socket;
    int player_sockets[MAX_PLAYERS];
    pthread_mutex_t state_mutex;
    pthread_t accept_thread;
    pthread_t client_threads[MAX_PLAYERS];
    pthread_t turn_thread;
    int turn_loop_running;
};

// Factory method
ServerContext *server_create();
void server_destroy(ServerContext *ctx);

#endif // SERVER_API_H
