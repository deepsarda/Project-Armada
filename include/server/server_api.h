#ifndef SERVER_API_H
#define SERVER_API_H

#include <pthread.h>

#include "../common/events.h"
#include "../common/game_types.h"

typedef struct ServerContext
{
    GameState game_state;
    int running;
    int max_players;

    int server_socket;
    int player_sockets[MAX_PLAYERS];
    pthread_mutex_t state_mutex;
    pthread_t accept_thread;
} ServerContext;

ServerContext *server_create();
void server_destroy(ServerContext *ctx);

int server_init(ServerContext *ctx, int max_players);
void server_start(ServerContext *ctx);
void server_stop(ServerContext *ctx);

#endif // SERVER_API_H
