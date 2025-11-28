#include "../../include/server/server_api.h"
#include "../../include/server/main.h"
#include "../../include/networking/network.h"
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

// Arguments passed to each client thread
typedef struct
{
    ServerContext *ctx;
    net_socket_t socket_fd;
} ClientThreadArgs;

// Thread entry points
static void *server_accept_thread(void *arg);
static void *server_client_thread(void *arg);

// Event handlers
static void server_handle_event(ServerContext *ctx, const GameEvent *event);
static void server_handle_player_join(ServerContext *ctx, net_socket_t sender_socket, const EventPayload_PlayerJoin *payload);
static void server_handle_user_action(ServerContext *ctx, const EventPayload_UserAction *payload);
static void server_handle_match_start_request(ServerContext *ctx, int requester_id);
static void server_handle_disconnect(ServerContext *ctx, net_socket_t socket_fd);

// Event sending helpers
static void server_broadcast_event(ServerContext *ctx, const GameEvent *event);
static void server_send_event_to(ServerContext *ctx, int player_id, const GameEvent *event);
static void server_broadcast_current_turn(ServerContext *ctx, int is_match_start, const EventPayload_UserAction *last_action);

// Player management helpers
static PlayerState *server_get_player(ServerContext *ctx, int player_id);
static int server_find_open_slot(ServerContext *ctx);
static int server_find_player_by_socket(ServerContext *ctx, net_socket_t socket_fd);
static void server_reset_player(PlayerState *player, int player_id, const char *name);
static void server_refresh_player_count(ServerContext *ctx);

// Game state helpers
static void server_start_match(ServerContext *ctx);
static void server_emit_turn_event(ServerContext *ctx, EventType type, int turn_number, int current_id, int next_id, int is_match_start, const EventPayload_UserAction *last_action);
static void server_advance_turn(ServerContext *ctx, const EventPayload_UserAction *last_action);
static int server_next_active_player(ServerContext *ctx, int start_after);
static int server_compute_valid_actions(ServerContext *ctx, int player_id, int current_player_id);

// Misc helpers
static void server_emit_threshold_event(ServerContext *ctx, int player_id);
static void server_emit_host_update(ServerContext *ctx, int host_id, const char *host_name);
static int server_collect_active_players(ServerContext *ctx, int *out_ids, int max_ids);
static int server_build_player_snapshot(ServerContext *ctx, int viewer_id, PlayerGameState *out_state);
static int to_coarse_percent(int current, int max);
static int clamp_int(int value, int min, int max);
static int server_select_host_locked(ServerContext *ctx);
static void server_send_error_event(ServerContext *ctx, int player_id, int error_code, const char *message);
static int server_start_discovery_service(ServerContext *ctx);
static void server_stop_discovery_service(ServerContext *ctx);
static void *server_discovery_thread(void *arg);

// Create a new server context
ServerContext *server_create()
{
    ServerContext *ctx = (ServerContext *)malloc(sizeof(ServerContext));
    if (!ctx)
        return NULL;

    memset(ctx, 0, sizeof(ServerContext));
    ctx->max_players = MAX_PLAYERS;
    ctx->server_socket = NET_INVALID_SOCKET;
    ctx->discovery_socket = NET_INVALID_SOCKET;
    ctx->running = 0;
    ctx->accept_thread = 0;
    ctx->discovery_thread = 0;
    ctx->game_state.host_player_id = -1;
    ctx->game_state.turn.current_player_id = -1;
    ctx->game_state.winner_id = -1;
    for (int i = 0; i < MAX_PLAYERS; ++i)
    {
        ctx->player_sockets[i] = NET_INVALID_SOCKET;
    }
    pthread_mutex_init(&ctx->state_mutex, NULL);
    return ctx;
}

// Destroy server context and free resources
void server_destroy(ServerContext *ctx)
{
    if (!ctx)
        return;

    if (ctx->running)
    {
        server_stop(ctx);
    }

    pthread_mutex_destroy(&ctx->state_mutex);
    free(ctx);
}

// Initialize server context for a new game
int server_init(ServerContext *ctx, int max_players)
{
    if (!ctx)
        return -1;

    int clamped_max = max_players > 0 && max_players <= MAX_PLAYERS ? max_players : MAX_PLAYERS;
    ctx->max_players = clamped_max;

    pthread_mutex_lock(&ctx->state_mutex);
    memset(&ctx->game_state, 0, sizeof(GameState));
    ctx->game_state.turn.current_player_id = -1;
    ctx->game_state.turn.turn_number = 0;
    ctx->game_state.host_player_id = -1;
    ctx->game_state.winner_id = -1;
    pthread_mutex_unlock(&ctx->state_mutex);

    server_on_init(ctx);
    server_on_initialized(ctx, clamped_max);
    return 0;
}

// Start the server and begin accepting clients
void server_start(ServerContext *ctx)
{
    if (!ctx)
        return;

    server_on_starting(ctx, DEFAULT_PORT);
    ctx->server_socket = net_create_server_socket(DEFAULT_PORT);
    if (ctx->server_socket == NET_INVALID_SOCKET)
    {
        server_on_start_failed(ctx, "Failed to create socket");
        return;
    }

    ctx->running = 1;

    if (server_start_discovery_service(ctx) != 0)
    {
        fprintf(stderr, "[Server] Warning: LAN discovery responder unavailable.\n");
    }

    if (pthread_create(&ctx->accept_thread, NULL, server_accept_thread, ctx) != 0)
    {
        server_on_accept_thread_failed(ctx, "Failed to create accept thread");
        ctx->running = 0;
        net_close_socket(ctx->server_socket);
        ctx->server_socket = NET_INVALID_SOCKET;
        server_stop_discovery_service(ctx);
    }
    else
    {
        server_on_started(ctx, DEFAULT_PORT);
    }
}

// Stop the server and disconnect all clients
void server_stop(ServerContext *ctx)
{
    if (!ctx)
        return;

    server_on_stopping(ctx);
    ctx->running = 0;

    server_stop_discovery_service(ctx);

    if (ctx->server_socket != NET_INVALID_SOCKET)
    {
        net_close_socket(ctx->server_socket);
        ctx->server_socket = NET_INVALID_SOCKET;
    }

    if (ctx->accept_thread)
    {
        pthread_join(ctx->accept_thread, NULL);
        ctx->accept_thread = 0;
    }

    pthread_mutex_lock(&ctx->state_mutex);
    for (int i = 0; i < MAX_PLAYERS; ++i)
    {
        if (ctx->player_sockets[i] != NET_INVALID_SOCKET)
        {
            net_close_socket(ctx->player_sockets[i]);
            ctx->player_sockets[i] = NET_INVALID_SOCKET;
            ctx->game_state.players[i].is_active = 0;
            ctx->game_state.players[i].is_connected = 0;
        }
    }
    ctx->game_state.player_count = 0;
    ctx->game_state.match_started = 0;
    ctx->game_state.host_player_id = -1;
    ctx->game_state.is_game_over = 0;
    ctx->game_state.winner_id = -1;
    ctx->game_state.turn.current_player_id = -1;
    ctx->game_state.turn.turn_number = 0;
    pthread_mutex_unlock(&ctx->state_mutex);
}

static int server_start_discovery_service(ServerContext *ctx)
{
    if (!ctx)
        return -1;

    if (ctx->discovery_socket != NET_INVALID_SOCKET)
        return 0;

    ctx->discovery_socket = socket(AF_INET, SOCK_DGRAM, 0);
    if (ctx->discovery_socket == NET_INVALID_SOCKET)
    {
        net_log_socket_error("socket");
        return -1;
    }

    int reuse = 1;
    if (setsockopt(ctx->discovery_socket, SOL_SOCKET, SO_REUSEADDR, (const char *)&reuse, sizeof(reuse)) == NET_SOCKET_ERROR)
    {
        net_log_socket_error("setsockopt");
        net_close_socket(ctx->discovery_socket);
        ctx->discovery_socket = NET_INVALID_SOCKET;
        return -1;
    }

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons(DEFAULT_PORT);

    if (bind(ctx->discovery_socket, (struct sockaddr *)&addr, sizeof(addr)) == NET_SOCKET_ERROR)
    {
        net_log_socket_error("bind");
        net_close_socket(ctx->discovery_socket);
        ctx->discovery_socket = NET_INVALID_SOCKET;
        return -1;
    }

    if (pthread_create(&ctx->discovery_thread, NULL, server_discovery_thread, ctx) != 0)
    {
        fprintf(stderr, "[Server] Failed to start discovery thread.\n");
        net_close_socket(ctx->discovery_socket);
        ctx->discovery_socket = NET_INVALID_SOCKET;
        ctx->discovery_thread = 0;
        return -1;
    }

    return 0;
}

static void server_stop_discovery_service(ServerContext *ctx)
{
    if (!ctx)
        return;

    if (ctx->discovery_socket != NET_INVALID_SOCKET)
    {
        net_close_socket(ctx->discovery_socket);
        ctx->discovery_socket = NET_INVALID_SOCKET;
    }

    if (ctx->discovery_thread)
    {
        pthread_join(ctx->discovery_thread, NULL);
        ctx->discovery_thread = 0;
    }
}

static void *server_discovery_thread(void *arg)
{
    ServerContext *ctx = (ServerContext *)arg;
    while (ctx && ctx->running)
    {
        struct sockaddr_in client_addr;
        socklen_t addrlen = sizeof(client_addr);
        char buffer[128];
        ssize_t bytes = recvfrom(ctx->discovery_socket, buffer, sizeof(buffer) - 1, 0, (struct sockaddr *)&client_addr, &addrlen);
        if (bytes < 0)
        {
            if (!ctx->running)
            {
                break;
            }
            int last_error = NET_ERRNO();
            if (last_error == NET_EINTR)
            {
                continue;
            }
            if (last_error == NET_EBADF)
            {
                break;
            }
            continue;
        }

        buffer[bytes] = '\0';
        if (strncmp(buffer, ARMADA_DISCOVERY_REQUEST, strlen(ARMADA_DISCOVERY_REQUEST)) != 0)
        {
            continue;
        }

        int player_count = 0;
        pthread_mutex_lock(&ctx->state_mutex);
        player_count = ctx->game_state.player_count;
        pthread_mutex_unlock(&ctx->state_mutex);

        char response[128];
        snprintf(response, sizeof(response), "%s %d %d %d", ARMADA_DISCOVERY_RESPONSE, DEFAULT_PORT, player_count, ctx->max_players);
        sendto(ctx->discovery_socket, response, strlen(response), 0, (struct sockaddr *)&client_addr, addrlen);
    }

    return NULL;
}

// Accept thread: listens for new client connections
static void *server_accept_thread(void *arg)
{
    ServerContext *ctx = (ServerContext *)arg;
    struct sockaddr_in address;
    socklen_t addrlen = sizeof(address);

    server_on_accept_thread_started(ctx);

    while (ctx->running)
    {
        addrlen = sizeof(address);
        net_socket_t new_socket = accept(ctx->server_socket, (struct sockaddr *)&address, &addrlen);
        if (new_socket == NET_INVALID_SOCKET)
        {
            if (ctx->running)
                net_log_socket_error("accept");
            continue;
        }

        server_on_client_connected(ctx, new_socket);

        // Create a thread for this client
        ClientThreadArgs *args = malloc(sizeof(ClientThreadArgs));
        args->ctx = ctx;
        args->socket_fd = new_socket;

        pthread_t tid;
        if (pthread_create(&tid, NULL, server_client_thread, args) != 0)
        {
            server_on_accept_thread_failed(ctx, "Failed to create client thread");
            free(args);
            net_close_socket(new_socket);
        }
        else
        {
            pthread_detach(tid); // Detach so we don't have to join
        }
    }
    return NULL;
}

// Client thread: handles communication with a single client
static void *server_client_thread(void *arg)
{
    ClientThreadArgs *args = (ClientThreadArgs *)arg;
    ServerContext *ctx = args->ctx;
    net_socket_t sock = args->socket_fd;
    free(args);

    GameEvent event;
    while (ctx->running)
    {
        int result = net_receive_event(sock, &event);
        if (result <= 0)
        {
            server_on_client_disconnected(ctx, sock);
            break;
        }

        if (event.type == EVENT_PLAYER_JOIN_REQUEST)
        {
            server_handle_player_join(ctx, sock, &event.data.join_req);
            continue;
        }

        server_handle_event(ctx, &event);
    }

    // Cleanup
    net_close_socket(sock);

    // Remove from player_sockets if present
    server_handle_disconnect(ctx, sock);

    return NULL;
}

// Dispatch incoming events to appropriate handlers
static void server_handle_event(ServerContext *ctx, const GameEvent *event)
{
    if (!event)
        return;

    switch (event->type)
    {
    case EVENT_USER_ACTION:
        server_handle_user_action(ctx, &event->data.action);
        break;
    case EVENT_MATCH_START_REQUEST:
        server_handle_match_start_request(ctx, event->sender_id);
        break;
    default:
        server_on_unhandled_event(ctx, event->type);
        break;
    }
}

// Handle player join requests
static void server_handle_player_join(ServerContext *ctx, net_socket_t sender_socket, const EventPayload_PlayerJoin *payload)
{
    if (!payload || sender_socket == NET_INVALID_SOCKET)
        return;

    GameEvent ack_event;
    memset(&ack_event, 0, sizeof(GameEvent));
    ack_event.type = EVENT_PLAYER_JOIN_ACK;
    ack_event.timestamp = time(NULL);
    ack_event.data.join_ack.success = 0;
    ack_event.data.join_ack.player_id = -1;
    ack_event.data.join_ack.host_player_id = -1;
    ack_event.data.join_ack.is_host = 0;

    int host_changed = 0;
    int new_host_id = -1;
    char new_host_name[MAX_NAME_LEN] = {0};

    pthread_mutex_lock(&ctx->state_mutex);
    int slot = server_find_open_slot(ctx);
    if (slot == -1)
    {
        snprintf(ack_event.data.join_ack.message, sizeof(ack_event.data.join_ack.message), "Server full");
        ack_event.data.join_ack.host_player_id = ctx->game_state.host_player_id;
    }
    else
    {
        server_reset_player(&ctx->game_state.players[slot], slot, payload->player_name);
        ctx->player_sockets[slot] = sender_socket;
        server_refresh_player_count(ctx);
        ack_event.data.join_ack.success = 1;
        ack_event.data.join_ack.player_id = slot;
        snprintf(ack_event.data.join_ack.message, sizeof(ack_event.data.join_ack.message), "Welcome!");

        int previous_host = ctx->game_state.host_player_id;
        new_host_id = server_select_host_locked(ctx);
        ack_event.data.join_ack.host_player_id = ctx->game_state.host_player_id;
        ack_event.data.join_ack.is_host = (ctx->game_state.host_player_id == slot);
        if (new_host_id != previous_host)
        {
            host_changed = 1;
            if (new_host_id >= 0)
            {
                strncpy(new_host_name, ctx->game_state.players[new_host_id].name, MAX_NAME_LEN - 1);
                new_host_name[MAX_NAME_LEN - 1] = '\0';
            }
        }
    }
    pthread_mutex_unlock(&ctx->state_mutex);

    if (!ack_event.data.join_ack.success)
    {
        net_send_event(sender_socket, &ack_event);
        return;
    }

    server_send_event_to(ctx, ack_event.data.join_ack.player_id, &ack_event);

    // Notify all players of new join
    GameEvent lifecycle;
    memset(&lifecycle, 0, sizeof(GameEvent));
    lifecycle.type = EVENT_PLAYER_JOINED;
    lifecycle.timestamp = time(NULL);
    lifecycle.data.player_event.player_id = ack_event.data.join_ack.player_id;
    strncpy(lifecycle.data.player_event.player_name, payload->player_name, MAX_NAME_LEN - 1);
    server_broadcast_event(ctx, &lifecycle);

    server_broadcast_current_turn(ctx, 0, NULL);

    if (host_changed)
    {
        server_emit_host_update(ctx, new_host_id, new_host_name);
    }
}

// Handle user actions (gameplay)
static void server_handle_user_action(ServerContext *ctx, const EventPayload_UserAction *payload)
{
    if (!payload)
        return;

    ServerActionResult action_result;
    memset(&action_result, 0, sizeof(ServerActionResult));
    action_result.applied_action = *payload;
    action_result.winner_id = -1;

    int emit_threshold = 0;
    int threshold_player_id = -1;
    int conclude_game = 0;
    int winner_id = -1;
    char game_over_reason[64] = {0};

    pthread_mutex_lock(&ctx->state_mutex);

    // Validate turn and player
    if (!ctx->game_state.match_started || ctx->game_state.turn.current_player_id != payload->player_id)
    {
        pthread_mutex_unlock(&ctx->state_mutex);
        return;
    }

    PlayerState *player = server_get_player(ctx, payload->player_id);
    if (!player || !player->is_active)
    {
        pthread_mutex_unlock(&ctx->state_mutex);
        return;
    }

    // Apply action
    switch (payload->action_type)
    {
    case USER_ACTION_NONE:
        break;
    case USER_ACTION_END_TURN:
        break;
    case USER_ACTION_UPGRADE_PLANET:
    case USER_ACTION_UPGRADE_SHIP:
    case USER_ACTION_REPAIR_PLANET:
    case USER_ACTION_ATTACK_PLANET:
        server_on_turn_action(ctx, payload, &action_result);
        break;
    default:
        server_on_unknown_action(ctx, payload->action_type, payload->player_id);
        break;
    }

    // Check for game over
    if (action_result.game_over)
    {
        conclude_game = 1;
        winner_id = action_result.winner_id;
        if (action_result.reason[0])
        {
            strncpy(game_over_reason, action_result.reason, sizeof(game_over_reason) - 1);
        }
    }

    // Check for star goal
    if (!conclude_game && player->stars >= STAR_GOAL)
    {
        conclude_game = 1;
        winner_id = player->player_id;
        strncpy(game_over_reason, "Star goal reached", sizeof(game_over_reason) - 1);
    }

    // Check for star threshold warning
    if (player->stars >= STAR_WARNING_THRESHOLD && !player->has_crossed_threshold)
    {
        player->has_crossed_threshold = 1;
        emit_threshold = 1;
        threshold_player_id = player->player_id;
    }

    pthread_mutex_unlock(&ctx->state_mutex);

    if (emit_threshold)
    {
        server_emit_threshold_event(ctx, threshold_player_id);
    }

    // TODO: Fix... For deep
    if (conclude_game)
    {
        GameEvent over_event;
        memset(&over_event, 0, sizeof(GameEvent));
        over_event.type = EVENT_GAME_OVER;
        over_event.timestamp = time(NULL);
        over_event.data.game_over.winner_id = winner_id;
        if (!game_over_reason[0])
        {
            strncpy(game_over_reason, "Victory", sizeof(game_over_reason) - 1);
        }
        strncpy(over_event.data.game_over.reason, game_over_reason, sizeof(over_event.data.game_over.reason) - 1);

        pthread_mutex_lock(&ctx->state_mutex);
        ctx->game_state.match_started = 0;
        ctx->game_state.is_game_over = 1;
        ctx->game_state.winner_id = winner_id;
        pthread_mutex_unlock(&ctx->state_mutex);

        server_broadcast_event(ctx, &over_event);
        return;
    }

    // Advance to next turn
    server_advance_turn(ctx, &action_result.applied_action);
}

// Handle match start requests
static void server_handle_match_start_request(ServerContext *ctx, int requester_id)
{
    if (!ctx)
        return;

    pthread_mutex_lock(&ctx->state_mutex);
    if (requester_id < 0 || requester_id >= MAX_PLAYERS)
    {
        pthread_mutex_unlock(&ctx->state_mutex);
        return;
    }

    PlayerState *requester = server_get_player(ctx, requester_id);
    if (!requester || !requester->is_active)
    {
        pthread_mutex_unlock(&ctx->state_mutex);
        return;
    }

    int host_id = ctx->game_state.host_player_id;
    int match_started = ctx->game_state.match_started;
    int player_count = ctx->game_state.player_count;
    pthread_mutex_unlock(&ctx->state_mutex);

    if (match_started)
    {
        server_send_error_event(ctx, requester_id, 2001, "Match already started");
        return;
    }

    if (host_id != requester_id)
    {
        server_send_error_event(ctx, requester_id, 2002, "Only the lobby host can start the match");
        return;
    }

    if (player_count < MIN_PLAYERS)
    {
        server_send_error_event(ctx, requester_id, 2003, "Need at least 2 players to start");
        return;
    }

    server_start_match(ctx);
}

// Handle player disconnects
static void server_handle_disconnect(ServerContext *ctx, net_socket_t socket_fd)
{
    int host_changed = 0;
    int new_host_id = -1;
    char new_host_name[MAX_NAME_LEN] = {0};

    pthread_mutex_lock(&ctx->state_mutex);
    int player_id = server_find_player_by_socket(ctx, socket_fd);
    if (player_id == -1)
    {
        pthread_mutex_unlock(&ctx->state_mutex);
        return;
    }

    PlayerState *player = &ctx->game_state.players[player_id];
    char name_copy[MAX_NAME_LEN];
    strncpy(name_copy, player->name, sizeof(name_copy) - 1);
    name_copy[sizeof(name_copy) - 1] = '\0';
    player->is_active = 0;
    player->is_connected = 0;
    ctx->player_sockets[player_id] = NET_INVALID_SOCKET;
    server_refresh_player_count(ctx);

    int was_current = (ctx->game_state.turn.current_player_id == player_id);
    int previous_host = ctx->game_state.host_player_id;
    new_host_id = server_select_host_locked(ctx);
    if (new_host_id != previous_host)
    {
        host_changed = 1;
        if (new_host_id >= 0)
        {
            strncpy(new_host_name, ctx->game_state.players[new_host_id].name, MAX_NAME_LEN - 1);
            new_host_name[MAX_NAME_LEN - 1] = '\0';
        }
    }
    pthread_mutex_unlock(&ctx->state_mutex);

    // Notify all players of player leaving
    GameEvent lifecycle;
    memset(&lifecycle, 0, sizeof(GameEvent));
    lifecycle.type = EVENT_PLAYER_LEFT;
    lifecycle.timestamp = time(NULL);
    lifecycle.data.player_event.player_id = player_id;
    strncpy(lifecycle.data.player_event.player_name, name_copy, MAX_NAME_LEN - 1);
    server_broadcast_event(ctx, &lifecycle);

    // Advance turn if needed
    if (was_current)
    {
        server_advance_turn(ctx, NULL);
    }

    server_broadcast_current_turn(ctx, 0, NULL);

    if (host_changed)
    {
        server_emit_host_update(ctx, new_host_id, new_host_name);
    }
}

// Broadcast an event to all active players
static void server_broadcast_event(ServerContext *ctx, const GameEvent *event)
{
    pthread_mutex_lock(&ctx->state_mutex);
    for (int i = 0; i < MAX_PLAYERS; ++i)
    {
        net_socket_t sock = ctx->player_sockets[i];
        if (sock != NET_INVALID_SOCKET)
        {
            net_send_event(sock, event);
        }
    }
    pthread_mutex_unlock(&ctx->state_mutex);
}

// Send an event to a specific player
static void server_send_event_to(ServerContext *ctx, int player_id, const GameEvent *event)
{
    pthread_mutex_lock(&ctx->state_mutex);
    if (player_id >= 0 && player_id < MAX_PLAYERS)
    {
        net_socket_t sock = ctx->player_sockets[player_id];
        if (sock != NET_INVALID_SOCKET)
        {
            net_send_event(sock, event);
        }
    }
    pthread_mutex_unlock(&ctx->state_mutex);
}

// Collect IDs of all active players
static int server_collect_active_players(ServerContext *ctx, int *out_ids, int max_ids)
{
    if (!out_ids || max_ids <= 0)
    {
        return 0;
    }

    int count = 0;
    pthread_mutex_lock(&ctx->state_mutex);
    for (int i = 0; i < MAX_PLAYERS && count < max_ids; ++i)
    {
        if (ctx->game_state.players[i].is_active)
        {
            out_ids[count++] = i;
        }
    }
    pthread_mutex_unlock(&ctx->state_mutex);
    return count;
}

// Build a snapshot of game state for a specific viewer
static int server_build_player_snapshot(ServerContext *ctx, int viewer_id, PlayerGameState *out_state)
{
    if (!out_state)
    {
        return 0;
    }
    int previous_host = ctx->game_state.host_player_id;
    int host_changed = 0;
    int new_host_id = -1;
    char new_host_name[MAX_NAME_LEN] = {0};

    new_host_id = server_select_host_locked(ctx);
    if (new_host_id != previous_host)
    {
        host_changed = 1;
        if (new_host_id >= 0)
        {
            strncpy(new_host_name, ctx->game_state.players[new_host_id].name, MAX_NAME_LEN - 1);
            new_host_name[MAX_NAME_LEN - 1] = '\0';
        }
    }

    PlayerGameState snapshot;
    memset(&snapshot, 0, sizeof(PlayerGameState));

    pthread_mutex_lock(&ctx->state_mutex);
    PlayerState *viewer = server_get_player(ctx, viewer_id);
    if (!viewer || !viewer->is_active)
    {
        pthread_mutex_unlock(&ctx->state_mutex);
        return 0;
    }

    snapshot.viewer_id = viewer_id;
    snapshot.self = *viewer;

    for (int i = 0; i < MAX_PLAYERS; ++i)
    {
        PlayerPublicInfo info;
        memset(&info, 0, sizeof(info));
        PlayerState *candidate = &ctx->game_state.players[i];
        info.player_id = i;
        info.planet_level = candidate->planet.level;

        if (host_changed)
        {
            server_emit_host_update(ctx, new_host_id, new_host_name);
        }
        info.ship_level = candidate->ship.level;
        info.ship_base_damage = candidate->ship.base_damage;
        if (candidate->is_active)
        {
            if (viewer_id == i)
            {
                info.show_stars = 1;
                info.coarse_planet_health = (candidate->planet.max_health == 0) ? 0 : (candidate->planet.current_health * 100) / candidate->planet.max_health;
            }
            else
            {
                info.show_stars = candidate->stars >= STAR_WARNING_THRESHOLD;
                info.coarse_planet_health = to_coarse_percent(candidate->planet.current_health, candidate->planet.max_health);
            }
        }
        snapshot.entries[i] = info;
    }
    pthread_mutex_unlock(&ctx->state_mutex);

    *out_state = snapshot;
    return 1;
}

// Get pointer to player state by ID
static PlayerState *server_get_player(ServerContext *ctx, int player_id)
{
    if (player_id < 0 || player_id >= MAX_PLAYERS)
        return NULL;
    return &ctx->game_state.players[player_id];
}

// Find an open slot for a new player
static int server_find_open_slot(ServerContext *ctx)
{
    for (int i = 0; i < ctx->max_players && i < MAX_PLAYERS; ++i)
    {
        if (!ctx->game_state.players[i].is_active)
        {
            return i;
        }
    }
    return -1;
}

// Find player ID by socket FD
static int server_find_player_by_socket(ServerContext *ctx, net_socket_t socket_fd)
{
    for (int i = 0; i < MAX_PLAYERS; ++i)
    {
        if (ctx->player_sockets[i] == socket_fd)
        {
            return i;
        }
    }
    return -1;
}

// Reset player state for a new player
static void server_reset_player(PlayerState *player, int player_id, const char *name)
{
    memset(player, 0, sizeof(PlayerState));
    player->player_id = player_id;
    if (name)
    {
        strncpy(player->name, name, MAX_NAME_LEN - 1);
        player->name[MAX_NAME_LEN - 1] = '\0';
    }
    player->is_active = 1;
    player->is_connected = 1;
    player->stars = STARTING_STARS;
    player->planet.level = STARTING_PLANET_LEVEL;
    player->planet.max_health = STARTING_PLANET_MAX_HEALTH;
    player->planet.current_health = STARTING_PLANET_MAX_HEALTH;
    player->planet.base_income = STARTING_PLANET_INCOME;
    player->ship.level = STARTING_SHIP_LEVEL;
    player->ship.base_damage = STARTING_SHIP_BASE_DAMAGE;
    player->has_crossed_threshold = 0;
}

// Refresh player count in game state
static void server_refresh_player_count(ServerContext *ctx)
{
    int count = 0;
    for (int i = 0; i < MAX_PLAYERS; ++i)
    {
        if (ctx->game_state.players[i].is_active)
        {
            ++count;
        }
    }
    ctx->game_state.player_count = count;
}

// Start a new match
static void server_start_match(ServerContext *ctx)
{
    GameState snapshot;
    memset(&snapshot, 0, sizeof(GameState));

    pthread_mutex_lock(&ctx->state_mutex);
    if (ctx->game_state.match_started || ctx->game_state.player_count < MIN_PLAYERS)
    {
        pthread_mutex_unlock(&ctx->state_mutex);
        return;
    }

    int start_player = ctx->game_state.host_player_id;
    if (start_player < 0 || start_player >= MAX_PLAYERS || !ctx->game_state.players[start_player].is_active)
    {
        start_player = server_next_active_player(ctx, -1);
    }

    if (start_player == -1)
    {
        pthread_mutex_unlock(&ctx->state_mutex);
        return;
    }

    ctx->game_state.match_started = 1;
    ctx->game_state.is_game_over = 0;
    ctx->game_state.winner_id = -1;
    ctx->game_state.turn.turn_number = 1;
    ctx->game_state.turn.current_player_id = start_player;
    snapshot = ctx->game_state;
    pthread_mutex_unlock(&ctx->state_mutex);

    GameEvent start_event;
    memset(&start_event, 0, sizeof(GameEvent));
    start_event.type = EVENT_MATCH_START;
    start_event.timestamp = time(NULL);
    start_event.data.match_start.state = snapshot;
    server_broadcast_event(ctx, &start_event);

    server_broadcast_current_turn(ctx, 1, NULL);
}

// Compute valid actions for a player (must be called with mutex locked)
static int server_compute_valid_actions(ServerContext *ctx, int player_id, int current_player_id)
{
    int valid = 0;

    // Only the current player can take actions during their turn
    if (player_id != current_player_id)
    {
        return 0;
    }

    PlayerState *player = server_get_player(ctx, player_id);
    if (!player || !player->is_active)
    {
        return 0;
    }

    // End turn is always valid
    valid |= VALID_ACTION_END_TURN;

    // Attack planet: valid if there are other players to attack
    for (int i = 0; i < MAX_PLAYERS; ++i)
    {
        if (i != player_id && ctx->game_state.players[i].planet.current_health > 0 && ctx->game_state.players[i].is_active)
        {
            valid |= VALID_ACTION_ATTACK_PLANET;
            break;
        }
    }

    // Repair planet: valid if planet is damaged
    if (player->planet.current_health < player->planet.max_health)
    {
        valid |= VALID_ACTION_REPAIR_PLANET;
    }

    // Upgrade planet: always valid if player has stars. TODO: Calculate cost
    if (player->stars > 0)
    {
        valid |= VALID_ACTION_UPGRADE_PLANET;
    }

    // Upgrade ship: always valid if player has stars. TODO: Calculate cost
    if (player->stars > 0)
    {
        valid |= VALID_ACTION_UPGRADE_SHIP;
    }

    return valid;
}

// Emit a turn event to all players
static void server_emit_turn_event(ServerContext *ctx, EventType type, int turn_number, int current_id, int next_id, int is_match_start, const EventPayload_UserAction *last_action)
{
    int viewers[MAX_PLAYERS];
    int viewer_count = server_collect_active_players(ctx, viewers, MAX_PLAYERS);

    EventPayload_UserAction empty_action;
    memset(&empty_action, 0, sizeof(empty_action));
    empty_action.player_id = -1;
    empty_action.action_type = USER_ACTION_NONE;
    const EventPayload_UserAction *action_payload = last_action ? last_action : &empty_action;

    for (int i = 0; i < viewer_count; ++i)
    {
        PlayerGameState snapshot;
        if (!server_build_player_snapshot(ctx, viewers[i], &snapshot))
        {
            continue;
        }

        // Compute valid actions for this viewer
        pthread_mutex_lock(&ctx->state_mutex);
        int valid_actions = server_compute_valid_actions(ctx, viewers[i], current_id);
        pthread_mutex_unlock(&ctx->state_mutex);

        GameEvent event;
        memset(&event, 0, sizeof(GameEvent));
        event.type = type;
        event.timestamp = time(NULL);
        event.data.turn.current_player_id = current_id;
        event.data.turn.next_player_id = next_id;
        event.data.turn.turn_number = turn_number;
        event.data.turn.is_match_start = is_match_start;
        event.data.turn.valid_actions = valid_actions;
        event.data.turn.last_action = *action_payload;
        event.data.turn.game = snapshot;

        server_send_event_to(ctx, viewers[i], &event);
    }
}

// Broadcast current turn info to all players
static void server_broadcast_current_turn(ServerContext *ctx, int is_match_start, const EventPayload_UserAction *last_action)
{
    pthread_mutex_lock(&ctx->state_mutex);
    if (!ctx->game_state.match_started)
    {
        pthread_mutex_unlock(&ctx->state_mutex);
        return;
    }
    int current_id = ctx->game_state.turn.current_player_id;
    if (current_id < 0)
    {
        pthread_mutex_unlock(&ctx->state_mutex);
        return;
    }
    int turn_number = ctx->game_state.turn.turn_number;
    int next_id = server_next_active_player(ctx, current_id);
    pthread_mutex_unlock(&ctx->state_mutex);

    server_emit_turn_event(ctx, EVENT_TURN_STARTED, turn_number, current_id, next_id, is_match_start, last_action);
}

// Advance to the next player's turn
static void server_advance_turn(ServerContext *ctx, const EventPayload_UserAction *last_action)
{
    pthread_mutex_lock(&ctx->state_mutex);
    if (!ctx->game_state.match_started)
    {
        pthread_mutex_unlock(&ctx->state_mutex);
        return;
    }

    int next_player = server_next_active_player(ctx, ctx->game_state.turn.current_player_id);
    if (next_player == -1)
    {
        pthread_mutex_unlock(&ctx->state_mutex);
        return;
    }

    // Add planet income to the player whose turn is starting
    // Income = base_income * (current_health / max_health)
    PlayerState *next_player_state = server_get_player(ctx, next_player);
    if (next_player_state && next_player_state->is_active)
    {
        PlanetStats *planet = &next_player_state->planet;
        if (planet->max_health > 0)
        {
            // Calculate health ratio (scaled 0.0 to 1.0)
            double health_ratio = (double)planet->current_health / (double)planet->max_health;
            // Clamp to [0, 1]
            if (health_ratio < 0.0)
                health_ratio = 0.0;
            if (health_ratio > 1.0)
                health_ratio = 1.0;
            // Calculate income: base_income * health_ratio
            int income = (int)(planet->base_income * health_ratio);
            next_player_state->stars += income;
        }
    }

    ctx->game_state.turn.current_player_id = next_player;
    ctx->game_state.turn.turn_number += 1;
    int current_turn = ctx->game_state.turn.current_player_id;
    int turn_number = ctx->game_state.turn.turn_number;
    int following = server_next_active_player(ctx, current_turn);
    pthread_mutex_unlock(&ctx->state_mutex);
    server_emit_turn_event(ctx, EVENT_TURN_STARTED, turn_number, current_turn, following, 0, last_action);
}

// Find the next active player after a given player
static int server_next_active_player(ServerContext *ctx, int start_after)
{
    if (ctx->game_state.player_count == 0)
        return -1;

    for (int offset = 1; offset <= MAX_PLAYERS; ++offset)
    {
        int candidate = (start_after + offset + MAX_PLAYERS) % MAX_PLAYERS;
        if (ctx->game_state.players[candidate].is_active)
        {
            if (start_after >= 0 && start_after < MAX_PLAYERS && ctx->game_state.players[start_after].is_active && candidate == start_after)
            {
                continue;
            }
            return candidate;
        }
    }
    return -1;
}

// Emit a star threshold event
static void server_emit_threshold_event(ServerContext *ctx, int player_id)
{
    GameEvent threshold;
    memset(&threshold, 0, sizeof(GameEvent));
    threshold.type = EVENT_STAR_THRESHOLD_REACHED;
    threshold.timestamp = time(NULL);
    threshold.data.threshold.player_id = player_id;
    threshold.data.threshold.threshold = STAR_WARNING_THRESHOLD;
    server_broadcast_event(ctx, &threshold);
}

// Emit host update event
static void server_emit_host_update(ServerContext *ctx, int host_id, const char *host_name)
{
    GameEvent update;
    memset(&update, 0, sizeof(GameEvent));
    update.type = EVENT_HOST_UPDATED;
    update.timestamp = time(NULL);
    update.data.host_update.host_player_id = host_id;
    if (host_name && host_name[0] != '\0')
    {
        strncpy(update.data.host_update.host_player_name, host_name, MAX_NAME_LEN - 1);
        update.data.host_update.host_player_name[MAX_NAME_LEN - 1] = '\0';
    }
    else
    {
        update.data.host_update.host_player_name[0] = '\0';
    }
    server_broadcast_event(ctx, &update);
}

// Send error event to a player
static void server_send_error_event(ServerContext *ctx, int player_id, int error_code, const char *message)
{
    if (player_id < 0)
        return;

    GameEvent error_event;
    memset(&error_event, 0, sizeof(GameEvent));
    error_event.type = EVENT_ERROR;
    error_event.timestamp = time(NULL);
    error_event.data.error.error_code = error_code;
    if (message)
    {
        strncpy(error_event.data.error.message, message, sizeof(error_event.data.error.message) - 1);
    }
    else
    {
        strncpy(error_event.data.error.message, "Unknown error", sizeof(error_event.data.error.message) - 1);
    }
    error_event.data.error.message[sizeof(error_event.data.error.message) - 1] = '\0';

    server_send_event_to(ctx, player_id, &error_event);
}

// Select the host player (must be called with mutex locked)
static int server_select_host_locked(ServerContext *ctx)
{
    if (!ctx)
        return -1;

    int current = ctx->game_state.host_player_id;
    if (current >= 0 && current < MAX_PLAYERS && ctx->game_state.players[current].is_active)
    {
        return current;
    }

    for (int i = 0; i < MAX_PLAYERS; ++i)
    {
        if (ctx->game_state.players[i].is_active)
        {
            ctx->game_state.host_player_id = i;
            return i;
        }
    }

    ctx->game_state.host_player_id = -1;
    return -1;
}

// Convert health to coarse percent (25/50/75/100)
static int to_coarse_percent(int current, int max)
{
    if (max <= 0)
        return 0;
    int pct = (current * 100) / max;
    if (pct <= 0)
        return 0;
    if (pct <= 25)
        return 25;
    if (pct <= 50)
        return 50;
    if (pct <= 75)
        return 75;
    return 100;
}

// Clamp integer value between min and max
static int clamp_int(int value, int min, int max)
{
    if (value < min)
        return min;
    if (value > max)
        return max;
    return value;
}
