#ifndef SERVER_MAIN_H
#define SERVER_MAIN_H

#include "../server/server_api.h"

int server_main_init(ServerContext *ctx);
void server_main_on_initialized(ServerContext *ctx, int max_players);
void server_main_on_starting(ServerContext *ctx, int port);
void server_main_on_start_failed(ServerContext *ctx, const char *message);
void server_main_on_started(ServerContext *ctx, int port);
void server_main_on_accept_thread_started(ServerContext *ctx);
void server_main_on_accept_thread_failed(ServerContext *ctx, const char *message);
void server_main_on_stopping(ServerContext *ctx);
void server_main_on_client_connected(ServerContext *ctx, int socket_fd);
void server_main_on_client_disconnected(ServerContext *ctx, int socket_fd);
void server_main_on_unhandled_event(ServerContext *ctx, EventType type);
void server_main_on_unknown_action(ServerContext *ctx, UserActionType action, int player_id);

#endif // SERVER_MAIN_H
