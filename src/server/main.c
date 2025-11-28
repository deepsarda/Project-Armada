#include "../../include/server/main.h"
#include "../../include/client/ui_notifications.h"
#include <stdio.h>
#include <string.h>

int server_on_init(ServerContext *ctx)
{
    (void)ctx;
    return 0;
}

void server_on_initialized(ServerContext *ctx, int max_players)
{
    (void)ctx;
    armada_server_logf("[Server] Initialized for up to %d players.", max_players);
}

void server_on_starting(ServerContext *ctx, int port)
{
    (void)ctx;
    armada_server_logf("[Server] Starting server on port %d...", port);
}

void server_on_start_failed(ServerContext *ctx, const char *message)
{
    (void)ctx;
    armada_server_logf("[Server] Failed to start: %s", message ? message : "unknown error");
}

void server_on_started(ServerContext *ctx, int port)
{
    (void)ctx;
    armada_server_logf("[Server] Server listening on port %d.", port);
}

void server_on_accept_thread_started(ServerContext *ctx)
{
    (void)ctx;
    armada_server_logf("[Server] Accept thread running.");
}

void server_on_accept_thread_failed(ServerContext *ctx, const char *message)
{
    (void)ctx;
    armada_server_logf("[Server] Accept thread failed: %s", message ? message : "unknown error");
}

void server_on_stopping(ServerContext *ctx)
{
    (void)ctx;
    armada_server_logf("[Server] Stopping server...");
}

void server_on_client_connected(ServerContext *ctx, net_socket_t socket_fd)
{
    (void)ctx;
    armada_server_logf("[Server] Client connected on socket %llu.", (unsigned long long)socket_fd);
}

void server_on_client_disconnected(ServerContext *ctx, net_socket_t socket_fd)
{
    (void)ctx;
    armada_server_logf("[Server] Client disconnected on socket %llu.", (unsigned long long)socket_fd);
}

void server_on_unhandled_event(ServerContext *ctx, EventType type)
{
    (void)ctx;
    armada_server_logf("[Server] Unhandled event type %d.", type);
}

void server_on_unknown_action(ServerContext *ctx, UserActionType action, int player_id)
{
    (void)ctx;
    armada_server_logf("[Server] Unknown action %d from player %d.", action, player_id);
}

void server_on_turn_action(ServerContext *ctx, const EventPayload_UserAction *action, ServerActionResult *result)
{
    (void)ctx;
    if (!result)
    {
        return;
    }

    if (action)
    {
        // TODO: Enter logic to process the action here
        result->applied_action = *action;
        armada_server_logf("[Server] Processing action %d from player %d targeting %d (value=%d meta=%d).",
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
