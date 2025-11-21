#include "../../include/client/client_api.h"
#include "../../include/networking/network.h"
#include "../../include/client/main.h"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

static void client_handle_event(ClientContext *ctx, const GameEvent *event);

ClientContext *client_create(const char *name)
{
    ClientContext *ctx = (ClientContext *)malloc(sizeof(ClientContext));
    if (!ctx)
        return NULL;

    memset(ctx, 0, sizeof(ClientContext));
    client_init(ctx, name ? name : "Player");
    return ctx;
}

void client_destroy(ClientContext *ctx)
{
    if (!ctx)
        return;
    if (ctx->connected)
    {
        client_disconnect(ctx);
    }
    free(ctx);
}

int client_init(ClientContext *ctx, const char *player_name)
{
    if (!ctx)
        return -1;

    if (player_name)
    {
        strncpy(ctx->player_name, player_name, sizeof(ctx->player_name) - 1);
        ctx->player_name[sizeof(ctx->player_name) - 1] = '\0';
    }
    else if (ctx->player_name[0] == '\0')
    {
        strncpy(ctx->player_name, "Player", sizeof(ctx->player_name) - 1);
        ctx->player_name[sizeof(ctx->player_name) - 1] = '\0';
    }

    ctx->player_id = -1;
    ctx->connected = 0;
    ctx->socket_fd = -1;
    ctx->has_state_snapshot = 0;
    memset(&ctx->player_game_state, 0, sizeof(PlayerGameState));

    return client_main_init(ctx, ctx->player_name);
}

int client_connect(ClientContext *ctx, const char *server_addr)
{
    if (!ctx)
        return -1;

    const char *addr = server_addr ? server_addr : "127.0.0.1";
    client_main_on_connecting(ctx, addr, DEFAULT_PORT);

    ctx->socket_fd = net_connect_to_server(addr, DEFAULT_PORT);
    if (ctx->socket_fd < 0)
    {
        client_main_on_connection_failed(ctx, addr, DEFAULT_PORT);
        return -1;
    }

    ctx->connected = 1;
    client_main_on_connected(ctx);

    GameEvent join_event;
    memset(&join_event, 0, sizeof(GameEvent));
    join_event.type = EVENT_PLAYER_JOIN_REQUEST;
    join_event.sender_id = 0;
    join_event.timestamp = time(NULL);
    strncpy(join_event.data.join_req.player_name, ctx->player_name, sizeof(join_event.data.join_req.player_name) - 1);

    client_main_on_join_request(ctx);
    net_send_event(ctx->socket_fd, &join_event);
    return 0;
}

void client_disconnect(ClientContext *ctx)
{
    if (!ctx)
        return;

    if (ctx->connected)
    {
        client_main_on_disconnected(ctx);
    }
    ctx->connected = 0;
    if (ctx->socket_fd >= 0)
    {
        net_close_socket(ctx->socket_fd);
        ctx->socket_fd = -1;
    }
}

void client_send_action(ClientContext *ctx, UserActionType action_type, int target_player_id, int value, int metadata)
{
    if (!ctx || !ctx->connected || ctx->player_id == -1)
        return;

    GameEvent action_event;
    memset(&action_event, 0, sizeof(GameEvent));
    action_event.type = EVENT_USER_ACTION;
    action_event.sender_id = ctx->player_id;
    action_event.timestamp = time(NULL);
    action_event.data.action.player_id = ctx->player_id;
    action_event.data.action.action_type = action_type;
    action_event.data.action.target_player_id = target_player_id;
    action_event.data.action.value = value;
    action_event.data.action.metadata = metadata;

    client_main_on_action_sent(ctx, action_type, target_player_id, value, metadata);
    net_send_event(ctx->socket_fd, &action_event);
}

void client_pump(ClientContext *ctx)
{
    if (!ctx || !ctx->connected)
        return;

    GameEvent event;
    int result = net_receive_event_flags(ctx->socket_fd, &event, MSG_DONTWAIT);

    if (result == 0)
    {
        return;
    }

    if (result < 0)
    {
        ctx->connected = 0;
        client_main_on_disconnected(ctx);
        if (ctx->socket_fd >= 0)
        {
            net_close_socket(ctx->socket_fd);
            ctx->socket_fd = -1;
        }
        return;
    }

    client_handle_event(ctx, &event);
}

static void client_handle_event(ClientContext *ctx, const GameEvent *event)
{
    if (!ctx || !event)
        return;

    switch (event->type)
    {
    case EVENT_PLAYER_JOIN_ACK:
        if (event->data.join_ack.success)
        {
            ctx->player_id = event->data.join_ack.player_id;
        }
        client_main_on_join_ack(ctx, &event->data.join_ack);
        break;
    case EVENT_PLAYER_JOINED:
        client_main_on_player_joined(ctx, &event->data.player_event);
        break;
    case EVENT_PLAYER_LEFT:
        client_main_on_player_left(ctx, &event->data.player_event);
        break;
    case EVENT_MATCH_START:
        client_main_on_match_start(ctx);
        break;
    case EVENT_MATCH_STOP:
        client_main_on_match_stop(ctx, &event->data.error);
        break;
    case EVENT_TURN_STARTED:
    case EVENT_TURN_COMPLETED:
    case EVENT_TURN_TIMEOUT:
        client_main_on_turn_event(ctx, event->type, &event->data.turn);
        break;
    case EVENT_STAR_THRESHOLD_REACHED:
        client_main_on_threshold(ctx, &event->data.threshold);
        break;
    case EVENT_STATE_UPDATE:
        client_main_on_state_update(ctx, &event->data.state_update.game);
        break;
    case EVENT_GAME_OVER:
        client_main_on_game_over(ctx, event->data.game_over.winner_id);
        break;
    default:
        break;
    }
}
