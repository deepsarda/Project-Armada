#include "../../include/client/client_api.h"
#include "../../include/networking/network.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <unistd.h>

static int client_init_impl(ClientContext *ctx, const char *player_name);
static void client_connect_impl(ClientContext *ctx, const char *server_addr);
static void client_disconnect_impl(ClientContext *ctx);
static void client_send_action_impl(ClientContext *ctx, int x, int y, int action_type);
static void client_handle_event_impl(ClientContext *ctx, const GameEvent *event);
static void client_on_match_start_impl(ClientContext *ctx);
static void client_on_state_update_impl(ClientContext *ctx, const GameState *game);
static void client_on_game_over_impl(ClientContext *ctx, int winner_id);

static void *client_listen_thread(void *arg);

static ClientInterface client_vtable = {
    .init = client_init_impl,
    .connect = client_connect_impl,
    .disconnect = client_disconnect_impl,
    .send_action = client_send_action_impl,
    .handle_event = client_handle_event_impl,
    .on_match_start = client_on_match_start_impl,
    .on_state_update = client_on_state_update_impl,
    .on_game_over = client_on_game_over_impl};

ClientContext *client_create(const char *name)
{
    ClientContext *ctx = (ClientContext *)malloc(sizeof(ClientContext));
    if (ctx)
    {
        ctx->vtable = &client_vtable;
        strncpy(ctx->player_name, name, 31);
        ctx->player_id = -1; // Unknown until join ack
        ctx->connected = 0;
        ctx->socket_fd = -1;
    }
    return ctx;
}

void client_destroy(ClientContext *ctx)
{
    if (ctx)
        free(ctx);
}

static int client_init_impl(ClientContext *ctx, const char *player_name)
{
    (void)player_name; // Unused
    printf("[Client %s] Initialized.\n", ctx->player_name);
    return 0;
}

static void client_connect_impl(ClientContext *ctx, const char *server_addr)
{
    printf("[Client %s] Connecting to %s:%d...\n", ctx->player_name, server_addr, DEFAULT_PORT);

    ctx->socket_fd = net_connect_to_server(server_addr, DEFAULT_PORT);
    if (ctx->socket_fd < 0)
    {
        printf("[Client %s] Connection failed.\n", ctx->player_name);
        return;
    }

    ctx->connected = 1;

    // Start listener thread
    if (pthread_create(&ctx->listen_thread, NULL, client_listen_thread, ctx) != 0)
    {
        perror("pthread_create");
        close(ctx->socket_fd);
        ctx->connected = 0;
        return;
    }

    // Send join request
    GameEvent join_event;
    join_event.type = EVENT_PLAYER_JOIN_REQUEST;
    join_event.sender_id = 0; // Temporary ID
    join_event.timestamp = time(NULL);
    strncpy(join_event.data.join_req.player_name, ctx->player_name, 31);

    printf("[Client %s] Sending Join Request...\n", ctx->player_name);
    net_send_event(ctx->socket_fd, &join_event);
}

static void client_disconnect_impl(ClientContext *ctx)
{
    printf("[Client %s] Disconnecting...\n", ctx->player_name);
    ctx->connected = 0;
    net_close_socket(ctx->socket_fd);

    // TODO: Join thread
}

static void client_send_action_impl(ClientContext *ctx, int x, int y, int action_type)
{
    if (!ctx->connected || ctx->player_id == -1)
    {
        printf("[Client %s] Cannot send action: Not connected or not joined.\n", ctx->player_name);
        return;
    }

    GameEvent action_event;
    action_event.type = EVENT_USER_ACTION;
    action_event.sender_id = ctx->player_id;
    action_event.timestamp = time(NULL);
    action_event.data.action.player_id = ctx->player_id;
    action_event.data.action.x = x;
    action_event.data.action.y = y;
    action_event.data.action.action_type = action_type;

    printf("[Client %s] Sending Action: (%d, %d)\n", ctx->player_name, x, y);
    net_send_event(ctx->socket_fd, &action_event);
}

static void client_handle_event_impl(ClientContext *ctx, const GameEvent *event)
{
    switch (event->type)
    {
    case EVENT_PLAYER_JOIN_ACK:
        if (event->data.join_ack.success)
        {
            ctx->player_id = event->data.join_ack.player_id;
            printf("[Client %s] Joined successfully! Assigned ID: %d\n", ctx->player_name, ctx->player_id);
        }
        else
        {
            printf("[Client %s] Join failed: %s\n", ctx->player_name, event->data.join_ack.message);
        }
        break;
    case EVENT_MATCH_START:
        ctx->vtable->on_match_start(ctx);
        break;
    case EVENT_STATE_UPDATE:
        ctx->vtable->on_state_update(ctx, &event->data.state_update.game);
        break;
    case EVENT_GAME_OVER:
        ctx->vtable->on_game_over(ctx, event->data.game_over.winner_id);
        break;
    default:
        break;
    }
}

static void client_on_match_start_impl(ClientContext *ctx)
{
    printf("[Client %s] Match Started! Good luck.\n", ctx->player_name);
}

static void client_on_state_update_impl(ClientContext *ctx, const GameState *game)
{
    printf("[Client %s] Game Updated.\n", ctx->player_name);
}

static void client_on_game_over_impl(ClientContext *ctx, int winner_id)
{
    printf("[Client %s] Game Over. Winner: %d\n", ctx->player_name, winner_id);
}

static void *client_listen_thread(void *arg)
{
    ClientContext *ctx = (ClientContext *)arg;
    GameEvent event;

    while (ctx->connected)
    {
        int result = net_receive_event(ctx->socket_fd, &event);
        if (result <= 0)
        {
            printf("[Client %s] Disconnected from server.\n", ctx->player_name);
            ctx->connected = 0;
            break;
        }

        ctx->vtable->handle_event(ctx, &event);
    }
    return NULL;
}
