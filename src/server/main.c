#include "../../include/server/main.h"
#include <stdio.h>
#include <string.h>

int server_main_init(ServerContext *ctx)
{
    (void)ctx;
    return 0;
}

void server_main_on_initialized(ServerContext *ctx, int max_players)
{
    (void)ctx;
    printf("[ServerMain] Initialized for up to %d players.\n", max_players);
}

void server_main_on_starting(ServerContext *ctx, int port)
{
    (void)ctx;
    printf("[ServerMain] Starting server on port %d...\n", port);
}

void server_main_on_start_failed(ServerContext *ctx, const char *message)
{
    (void)ctx;
    fprintf(stderr, "[ServerMain] Failed to start: %s\n", message ? message : "unknown error");
}

void server_main_on_started(ServerContext *ctx, int port)
{
    (void)ctx;
    printf("[ServerMain] Server listening on port %d.\n", port);
}

void server_main_on_accept_thread_started(ServerContext *ctx)
{
    (void)ctx;
    printf("[ServerMain] Accept thread running.\n");
}

void server_main_on_accept_thread_failed(ServerContext *ctx, const char *message)
{
    (void)ctx;
    fprintf(stderr, "[ServerMain] Accept thread failed: %s\n", message ? message : "unknown error");
}

void server_main_on_stopping(ServerContext *ctx)
{
    (void)ctx;
    printf("[ServerMain] Stopping server...\n");
}

void server_main_on_client_connected(ServerContext *ctx, int socket_fd)
{
    (void)ctx;
    printf("[ServerMain] Client connected on socket %d.\n", socket_fd);
}

void server_main_on_client_disconnected(ServerContext *ctx, int socket_fd)
{
    (void)ctx;
    printf("[ServerMain] Client disconnected on socket %d.\n", socket_fd);
}

void server_main_on_unhandled_event(ServerContext *ctx, EventType type)
{
    (void)ctx;
    printf("[ServerMain] Unhandled event type %d.\n", type);
}

void server_main_on_unknown_action(ServerContext *ctx, UserActionType action, int player_id)
{
    (void)ctx;
    printf("[ServerMain] Unknown action %d from player %d.\n", action, player_id);
}

void server_main_on_turn_action(ServerContext *ctx, const EventPayload_UserAction *action, ServerActionResult *result)
{
    (void)ctx;
    if (!result)
    {
        return;
    }

    if (action)
    {
        result->applied_action = *action;
        printf("[ServerMain] Processing action %d from player %d targeting %d (value=%d meta=%d).\n",
               action->action_type,
               action->player_id,
               action->target_player_id,
               action->value,
               action->metadata);
    }
    else
    {
        memset(&result->applied_action, 0, sizeof(result->applied_action));
    }

    if (result->winner_id >= 0)
    {
        result->game_over = 1;
    }
}
