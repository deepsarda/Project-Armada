#include "../../include/server/server_api.h"
#include "../../include/networking/network.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

// Forward declarations of implementation methods
static int server_init_impl(ServerContext *ctx, int max_players);
static void server_start_impl(ServerContext *ctx);
static void server_stop_impl(ServerContext *ctx);
static void server_broadcast_event_impl(ServerContext *ctx, const GameEvent *event);
static void server_send_event_to_impl(ServerContext *ctx, int player_id, const GameEvent *event);
static void server_process_event_impl(ServerContext *ctx, const GameEvent *event);
static void server_on_player_join_impl(ServerContext *ctx, int sender_id, const EventPayload_PlayerJoin *payload);
static void server_on_user_action_impl(ServerContext *ctx, const EventPayload_UserAction *payload);

// Thread functions
static void *server_accept_thread(void *arg);
static void *server_client_thread(void *arg);

typedef struct
{
    ServerContext *ctx;
    int socket_fd;
} ClientThreadArgs;

// VTable instance
static ServerInterface server_vtable = {
    .init = server_init_impl,
    .start = server_start_impl,
    .stop = server_stop_impl,
    .broadcast_event = server_broadcast_event_impl,
    .send_event_to = server_send_event_to_impl,
    .process_event = server_process_event_impl,
    .on_player_join = server_on_player_join_impl,
    .on_user_action = server_on_user_action_impl};

ServerContext *server_create()
{
    ServerContext *ctx = (ServerContext *)malloc(sizeof(ServerContext));
    if (ctx)
    {
        ctx->vtable = &server_vtable;
        memset(&ctx->game_state, 0, sizeof(GameState));
        ctx->running = 0;
        ctx->server_socket = -1;
        for (int i = 0; i < MAX_PLAYERS; i++)
            ctx->player_sockets[i] = -1;
        pthread_mutex_init(&ctx->state_mutex, NULL);
    }
    return ctx;
}

void server_destroy(ServerContext *ctx)
{
    if (ctx)
    {
        pthread_mutex_destroy(&ctx->state_mutex);
        free(ctx);
    }
}

static int server_init_impl(ServerContext *ctx, int max_players)
{
    printf("[Server] Initializing for max %d players...\n", max_players);
    ctx->game_state.player_count = 0;
    ctx->game_state.match_started = 0;
    return 0;
}

static void server_start_impl(ServerContext *ctx)
{
    printf("[Server] Starting on port %d...\n", DEFAULT_PORT);
    ctx->server_socket = net_create_server_socket(DEFAULT_PORT);
    if (ctx->server_socket < 0)
    {
        fprintf(stderr, "[Server] Failed to create socket.\n");
        return;
    }

    ctx->running = 1;
    if (pthread_create(&ctx->accept_thread, NULL, server_accept_thread, ctx) != 0)
    {
        perror("[Server] Failed to create accept thread");
        ctx->running = 0;
    }
}

static void server_stop_impl(ServerContext *ctx)
{
    printf("[Server] Stopping...\n");
    ctx->running = 0;
    net_close_socket(ctx->server_socket);
    // TODO: join threads and close client sockets
}

static void server_broadcast_event_impl(ServerContext *ctx, const GameEvent *event)
{
    printf("[Server] Broadcasting event type %d to all players.\n", event->type);
    pthread_mutex_lock(&ctx->state_mutex);
    for (int i = 0; i < MAX_PLAYERS; i++)
    {
        if (ctx->player_sockets[i] != -1)
        {
            net_send_event(ctx->player_sockets[i], event);
        }
    }
    pthread_mutex_unlock(&ctx->state_mutex);
}

static void server_send_event_to_impl(ServerContext *ctx, int player_id, const GameEvent *event)
{
    printf("[Server] Sending event type %d to player %d.\n", event->type, player_id);
    pthread_mutex_lock(&ctx->state_mutex);
    if (player_id >= 0 && player_id < MAX_PLAYERS && ctx->player_sockets[player_id] != -1)
    {
        net_send_event(ctx->player_sockets[player_id], event);
    }
    pthread_mutex_unlock(&ctx->state_mutex);
}

static void server_process_event_impl(ServerContext *ctx, const GameEvent *event)
{
    printf("[Server] Processing event type %d from sender %d\n", event->type, event->sender_id);

    switch (event->type)
    {
    case EVENT_PLAYER_JOIN_REQUEST:
        // For join request, sender_id is the socket fd
        ctx->vtable->on_player_join(ctx, event->sender_id, &event->data.join_req);
        break;
    case EVENT_USER_ACTION:
        ctx->vtable->on_user_action(ctx, &event->data.action);
        break;
    default:
        printf("[Server] Unhandled event type.\n");
        break;
    }
}

static void server_on_player_join_impl(ServerContext *ctx, int sender_socket, const EventPayload_PlayerJoin *payload)
{
    pthread_mutex_lock(&ctx->state_mutex);
    if (ctx->game_state.player_count >= MAX_PLAYERS)
    {
        printf("[Server] Join rejected: Server full.\n");
        pthread_mutex_unlock(&ctx->state_mutex);
        return;
    }

    int new_id = ctx->game_state.player_count++;
    Player *p = &ctx->game_state.players[new_id];
    p->id = new_id;
    strncpy(p->name, payload->player_name, 31);
    p->is_active = 1;

    // Map player ID to socket
    ctx->player_sockets[new_id] = sender_socket;

    printf("[Server] Player '%s' joined. Assigned ID %d to socket %d.\n", p->name, new_id, sender_socket);

    // Send ACK
    GameEvent ack_event;
    ack_event.type = EVENT_PLAYER_JOIN_ACK;
    ack_event.sender_id = -1;
    ack_event.timestamp = time(NULL);
    ack_event.data.join_ack.player_id = new_id;
    ack_event.data.join_ack.success = 1;
    strcpy(ack_event.data.join_ack.message, "Welcome!");

    pthread_mutex_unlock(&ctx->state_mutex);

    ctx->vtable->send_event_to(ctx, new_id, &ack_event);

    // Check if we should start match
    pthread_mutex_lock(&ctx->state_mutex);
    int should_start = (ctx->game_state.player_count >= MIN_PLAYERS && !ctx->game_state.match_started);
    if (should_start)
    {
        ctx->game_state.match_started = 1;
    }
    pthread_mutex_unlock(&ctx->state_mutex);

    if (should_start)
    {
        printf("[Server] Minimum players reached. Starting match...\n");
        GameEvent start_event;
        start_event.type = EVENT_MATCH_START;
        start_event.sender_id = -1;
        start_event.timestamp = time(NULL);
        ctx->vtable->broadcast_event(ctx, &start_event);
    }
}

static void server_on_user_action_impl(ServerContext *ctx, const EventPayload_UserAction *payload)
{
    printf("[Server] Action received from player %d: type %d at (%d, %d)\n",
           payload->player_id, payload->action_type, payload->x, payload->y);

    // TODO: Logic goes here

    // Broadcast update
    GameEvent update_event;
    update_event.type = EVENT_STATE_UPDATE;
    update_event.sender_id = -1;
    update_event.timestamp = time(NULL);
    update_event.data.state_update.game = ctx->game_state;

    ctx->vtable->broadcast_event(ctx, &update_event);
}

static void *server_accept_thread(void *arg)
{
    ServerContext *ctx = (ServerContext *)arg;
    struct sockaddr_in address;
    int addrlen = sizeof(address);

    printf("[Server] Accept thread started.\n");

    while (ctx->running)
    {
        int new_socket = accept(ctx->server_socket, (struct sockaddr *)&address, (socklen_t *)&addrlen);
        if (new_socket < 0)
        {
            if (ctx->running)
                perror("accept");
            continue;
        }

        printf("[Server] New connection accepted: socket %d\n", new_socket);

        // Create a thread for this client
        // TODO: might want to manage these threads better (e.g. pool)
        // For now, just detach or keep track?
        // don't have a slot for the thread handle until they join.
        // So we just malloc the args and let it run.

        ClientThreadArgs *args = malloc(sizeof(ClientThreadArgs));
        args->ctx = ctx;
        args->socket_fd = new_socket;

        pthread_t tid;
        if (pthread_create(&tid, NULL, server_client_thread, args) != 0)
        {
            perror("pthread_create");
            free(args);
            close(new_socket);
        }
        else
        {
            pthread_detach(tid); // Detach so we don't have to join
        }
    }
    return NULL;
}

static void *server_client_thread(void *arg)
{
    ClientThreadArgs *args = (ClientThreadArgs *)arg;
    ServerContext *ctx = args->ctx;
    int sock = args->socket_fd;
    free(args);

    GameEvent event;
    while (ctx->running)
    {
        int result = net_receive_event(sock, &event);
        if (result <= 0)
        {
            printf("[Server] Client disconnected (socket %d).\n", sock);
            break;
        }

        // If it's a join request, we need to pass the socket info
        if (event.type == EVENT_PLAYER_JOIN_REQUEST)
        {
            event.sender_id = sock; // HACK: pass socket as sender_id for join
        }

        // Process event
        // NOTE: process_event calls logic which might need locking.
        // The logic implementations (on_player_join, etc) should handle locking.
        ctx->vtable->process_event(ctx, &event);
    }

    // Cleanup
    net_close_socket(sock);

    // Remove from player_sockets if present
    pthread_mutex_lock(&ctx->state_mutex);
    for (int i = 0; i < MAX_PLAYERS; i++)
    {
        if (ctx->player_sockets[i] == sock)
        {
            ctx->player_sockets[i] = -1;
            ctx->game_state.players[i].is_active = 0;
            // Handle player leave logic here if needed
            break;
        }
    }
    pthread_mutex_unlock(&ctx->state_mutex);

    return NULL;
}
