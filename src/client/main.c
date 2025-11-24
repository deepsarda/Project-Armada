#include "../../include/client/main.h"
#include "../../include/client/ui_notifications.h"
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
    armada_ui_logf("[Client %s] Connected to server.", ctx->player_name);
}

void client_main_on_connecting(ClientContext *ctx, const char *server_addr, int port)
{
    armada_ui_logf("[Client %s] Connecting to %s:%d...", ctx->player_name, server_addr, port);
}

void client_main_on_connection_failed(ClientContext *ctx, const char *server_addr, int port)
{
    armada_ui_logf("[Client %s] Connection to %s:%d failed.", ctx->player_name, server_addr, port);
}

void client_main_on_disconnected(ClientContext *ctx)
{
    armada_ui_logf("[Client %s] Disconnected from server.", ctx->player_name);
}

void client_main_on_join_request(ClientContext *ctx)
{
    armada_ui_logf("[Client %s] Sending join request.", ctx->player_name);
}

void client_main_on_join_ack(ClientContext *ctx, const EventPayload_JoinAck *payload)
{
    if (!payload)
        return;
    if (payload->success)
    {
        armada_ui_logf("[Client %s] Joined successfully. Assigned ID %d.", ctx->player_name, payload->player_id);
        if (payload->is_host)
        {
            armada_ui_logf("[Client %s] You are the lobby host. Use 'start' to begin once ready.", ctx->player_name);
        }
        else if (payload->host_player_id >= 0)
        {
            armada_ui_logf("[Client %s] Waiting for host player %d to begin the match.",
                           ctx->player_name,
                           payload->host_player_id);
        }
    }
    else
    {
        armada_ui_logf("[Client %s] Join rejected: %s", ctx->player_name, payload->message);
    }
}

void client_main_on_player_joined(ClientContext *ctx, const EventPayload_PlayerLifecycle *payload)
{
    if (!payload)
        return;
    armada_ui_logf("[Client %s] Player %d joined (%s).",
                   ctx->player_name,
                   payload->player_id,
                   payload->player_name);
}

void client_main_on_host_update(ClientContext *ctx, const EventPayload_HostUpdate *payload)
{
    if (!payload)
        return;

    if (payload->host_player_id >= 0)
    {
        armada_ui_logf("[Client %s] Player %d (%s) is now the lobby host.",
                       ctx->player_name,
                       payload->host_player_id,
                       payload->host_player_name);
        if (ctx && ctx->player_id == payload->host_player_id)
        {
            armada_ui_logf("[Client %s] You are now the host. Type 'start' when ready.", ctx->player_name);
        }
    }
    else
    {
        armada_ui_logf("[Client %s] Lobby host cleared. Waiting for a new host.", ctx->player_name);
    }
}

void client_main_on_player_left(ClientContext *ctx, const EventPayload_PlayerLifecycle *payload)
{
    if (!payload)
        return;
    armada_ui_logf("[Client %s] Player %d left (%s).",
                   ctx->player_name,
                   payload->player_id,
                   payload->player_name);
}

void client_main_on_match_start(ClientContext *ctx, const EventPayload_MatchStart *payload)
{
    if (!payload || !ctx)
        return;

    ctx->host_player_id = payload->state.host_player_id;
    ctx->is_host = (ctx->player_id >= 0 && ctx->player_id == ctx->host_player_id);
    ctx->has_state_snapshot = 0;
    memset(&ctx->player_game_state, 0, sizeof(PlayerGameState));

    armada_ui_logf("[Client %s] Match started with %d players. First turn belongs to player %d.",
                   ctx->player_name,
                   payload->state.player_count,
                   payload->state.turn.current_player_id);
}

void client_main_on_match_stop(ClientContext *ctx, const EventPayload_Error *payload)
{
    const char *reason = (payload) ? payload->message : "Unknown";
    armada_ui_logf("[Client %s] Server message: %s", ctx->player_name, reason);
}

void client_main_on_turn_event(ClientContext *ctx, EventType type, const EventPayload_TurnInfo *payload)
{
    if (!payload)
        return;
    armada_ui_logf("[Client %s] Turn event %d: current %d next %d (#%d).",
                   ctx->player_name,
                   type,
                   payload->current_player_id,
                   payload->next_player_id,
                   payload->turn_number);

    if (payload->is_match_start)
    {
        armada_ui_logf("[Client %s] Match phase starting with this turn.", ctx->player_name);
    }

    if (payload->last_action.action_type != USER_ACTION_NONE)
    {
        armada_ui_logf("[Client %s] Last action: player %d type %d target %d value %d meta %d.",
                       ctx->player_name,
                       payload->last_action.player_id,
                       payload->last_action.action_type,
                       payload->last_action.target_player_id,
                       payload->last_action.value,
                       payload->last_action.metadata);
    }
}

void client_main_on_threshold(ClientContext *ctx, const EventPayload_Threshold *payload)
{
    if (!payload)
        return;
    armada_ui_logf("[Client %s] Player %d crossed %d stars.",
                   ctx->player_name,
                   payload->player_id,
                   payload->threshold);
}

void client_main_on_action_sent(ClientContext *ctx, UserActionType type, int target_player_id, int value, int metadata)
{
    armada_ui_logf("[Client %s] Action queued: type=%d target=%d value=%d meta=%d",
                   ctx->player_name,
                   type,
                   target_player_id,
                   value,
                   metadata);
}

void client_main_on_game_over(ClientContext *ctx, int winner_id)
{
    armada_ui_logf("[Client %s] Winner announced: %d", ctx->player_name, winner_id);
}
