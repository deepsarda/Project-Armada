#ifndef SERVER_API_H
#define SERVER_API_H

#include <pthread.h>

#include "../common/events.h"
#include "../common/game_types.h"
#include "../networking/net_platform.h"

typedef struct ServerContext
{
    GameState game_state;
    int running;
    int max_players;

    net_socket_t server_socket;
    net_socket_t player_sockets[MAX_PLAYERS];
    net_socket_t discovery_socket;
    pthread_mutex_t state_mutex;
    pthread_t accept_thread;
    pthread_t discovery_thread;
} ServerContext;

#ifdef __cplusplus
extern "C"
{
#endif

    ServerContext *server_create();
    void server_destroy(ServerContext *ctx);

    int server_init(ServerContext *ctx, int max_players);
    void server_start(ServerContext *ctx);
    void server_stop(ServerContext *ctx);

#ifdef __cplusplus
}
#endif

#endif // SERVER_API_H
