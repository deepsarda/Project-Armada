#include "../../include/server/main.h"
#include "../../include/client/ui_notifications.h"
#include <stdio.h>
#include <string.h>

// ANSI color codes for server logging
#define SRV_COLOR_RESET "\033[0m"
#define SRV_COLOR_GREEN "\033[32m"
#define SRV_COLOR_YELLOW "\033[33m"
#define SRV_COLOR_RED "\033[31m"
#define SRV_COLOR_CYAN "\033[36m"
#define SRV_COLOR_MAGENTA "\033[35m"
#define SRV_COLOR_BOLD "\033[1m"

int server_on_init(ServerContext *ctx)
{
    (void)ctx;
    return 0;
}

void server_on_initialized(ServerContext *ctx, int max_players)
{
    (void)ctx;
    armada_server_logf(SRV_COLOR_GREEN "[Server]" SRV_COLOR_RESET " Initialized for up to " SRV_COLOR_BOLD "%d" SRV_COLOR_RESET " players.", max_players);
}

void server_on_starting(ServerContext *ctx, int port)
{
    (void)ctx;
    armada_server_logf(SRV_COLOR_CYAN "[Server]" SRV_COLOR_RESET " Starting server on port " SRV_COLOR_BOLD "%d" SRV_COLOR_RESET "...", port);
}

void server_on_start_failed(ServerContext *ctx, const char *message)
{
    (void)ctx;
    armada_server_logf(SRV_COLOR_RED "[Server] ERROR:" SRV_COLOR_RESET " Failed to start: %s", message ? message : "unknown error");
}

void server_on_started(ServerContext *ctx, int port)
{
    (void)ctx;
    armada_server_logf(SRV_COLOR_GREEN "[Server]" SRV_COLOR_RESET " Server listening on port " SRV_COLOR_BOLD "%d" SRV_COLOR_RESET ".", port);
}

void server_on_accept_thread_started(ServerContext *ctx)
{
    (void)ctx;
    armada_server_logf(SRV_COLOR_GREEN "[Server]" SRV_COLOR_RESET " Accept thread running.");
}

void server_on_accept_thread_failed(ServerContext *ctx, const char *message)
{
    (void)ctx;
    armada_server_logf(SRV_COLOR_RED "[Server] ERROR:" SRV_COLOR_RESET " Accept thread failed: %s", message ? message : "unknown error");
}

void server_on_stopping(ServerContext *ctx)
{
    (void)ctx;
    armada_server_logf(SRV_COLOR_YELLOW "[Server]" SRV_COLOR_RESET " Stopping server...");
}

void server_on_client_connected(ServerContext *ctx, net_socket_t socket_fd)
{
    (void)ctx;
    armada_server_logf(SRV_COLOR_GREEN "[Server]" SRV_COLOR_RESET " Client connected on socket " SRV_COLOR_CYAN "%llu" SRV_COLOR_RESET ".", (unsigned long long)socket_fd);
}

void server_on_client_disconnected(ServerContext *ctx, net_socket_t socket_fd)
{
    (void)ctx;
    armada_server_logf(SRV_COLOR_YELLOW "[Server]" SRV_COLOR_RESET " Client disconnected from socket " SRV_COLOR_CYAN "%llu" SRV_COLOR_RESET ".", (unsigned long long)socket_fd);
}

void server_on_unhandled_event(ServerContext *ctx, EventType type)
{
    (void)ctx;
    armada_server_logf(SRV_COLOR_YELLOW "[Server]" SRV_COLOR_RESET " Unhandled event type " SRV_COLOR_MAGENTA "%d" SRV_COLOR_RESET ".", type);
}

void server_on_unknown_action(ServerContext *ctx, UserActionType action, int player_id)
{
    (void)ctx;
    armada_server_logf(SRV_COLOR_RED "[Server] WARNING:" SRV_COLOR_RESET " Unknown action " SRV_COLOR_MAGENTA "%d" SRV_COLOR_RESET " from player " SRV_COLOR_CYAN "%d" SRV_COLOR_RESET ".", action, player_id);
}
