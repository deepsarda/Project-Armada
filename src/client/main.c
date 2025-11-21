#include "../../include/client/main.h"
#include <stdio.h>
#include <string.h>

int client_main_init(ClientContext *ctx, const char *player_name)
{
    (void)player_name;
    ctx->has_state_snapshot = 0;
    memset(&ctx->player_game_state, 0, sizeof(PlayerGameState));
    return 0;
}

void client_main_on_connected(ClientContext *ctx)
{
    printf("[Client %s] Connected to server.\n", ctx->player_name);
}

void client_main_on_connecting(ClientContext *ctx, const char *server_addr, int port)
{
    printf("[Client %s] Connecting to %s:%d...\n", ctx->player_name, server_addr, port);
}

void client_main_on_connection_failed(ClientContext *ctx, const char *server_addr, int port)
{
    printf("[Client %s] Connection to %s:%d failed.\n", ctx->player_name, server_addr, port);
}

void client_main_on_disconnected(ClientContext *ctx)
{
    printf("[Client %s] Disconnected from server.\n", ctx->player_name);
}

void client_main_on_join_request(ClientContext *ctx)
{
    printf("[Client %s] Sending join request.\n", ctx->player_name);
}

void client_main_on_join_ack(ClientContext *ctx, const EventPayload_JoinAck *payload)
{
    if (!payload)
        return;
    if (payload->success)
    {
        printf("[Client %s] Joined successfully. Assigned ID %d.\n", ctx->player_name, payload->player_id);
    }
    else
    {
        printf("[Client %s] Join rejected: %s\n", ctx->player_name, payload->message);
    }
}

void client_main_on_player_joined(ClientContext *ctx, const EventPayload_PlayerLifecycle *payload)
{
    if (!payload)
        return;
    printf("[Client %s] Player %d joined (%s).\n",
           ctx->player_name,
           payload->player_id,
           payload->player_name);
}

void client_main_on_player_left(ClientContext *ctx, const EventPayload_PlayerLifecycle *payload)
{
    if (!payload)
        return;
    printf("[Client %s] Player %d left (%s).\n",
           ctx->player_name,
           payload->player_id,
           payload->player_name);
}

void client_main_on_match_start(ClientContext *ctx)
{
    printf("[Client %s] Match countdown complete.\n", ctx->player_name);
}

void client_main_on_match_stop(ClientContext *ctx, const EventPayload_Error *payload)
{
    const char *reason = (payload) ? payload->message : "Unknown";
    printf("[Client %s] Match stopped: %s\n", ctx->player_name, reason);
}

void client_main_on_turn_event(ClientContext *ctx, EventType type, const EventPayload_TurnInfo *payload)
{
    if (!payload)
        return;
    printf("[Client %s] Turn event %d: current %d next %d (#%d).\n",
           ctx->player_name,
           type,
           payload->current_player_id,
           payload->next_player_id,
           payload->turn_number);
}

void client_main_on_state_update(ClientContext *ctx, const PlayerGameState *player_game_state)
{
    if (!player_game_state)
        return;
    ctx->player_game_state = *player_game_state;
    ctx->has_state_snapshot = 1;
}

void client_main_on_threshold(ClientContext *ctx, const EventPayload_Threshold *payload)
{
    if (!payload)
        return;
    printf("[Client %s] Player %d crossed %d stars (now %d).\n",
           ctx->player_name,
           payload->player_id,
           payload->threshold,
           payload->current_total);
}

void client_main_on_action_sent(ClientContext *ctx, UserActionType type, int target_player_id, int value, int metadata)
{
    printf("[Client %s] Action queued: type=%d target=%d value=%d meta=%d\n",
           ctx->player_name,
           type,
           target_player_id,
           value,
           metadata);
}

void client_main_on_game_over(ClientContext *ctx, int winner_id)
{
    printf("[Client %s] Winner announced: %d\n", ctx->player_name, winner_id);
}
