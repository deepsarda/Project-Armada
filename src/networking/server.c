#include "../../include/server/server_api.h"
#include "../../include/server/main.h"
#include "../../include/networking/network.h"
#include <arpa/inet.h>
#include <netinet/in.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

typedef struct
{
    ServerContext *ctx;
    int socket_fd;
} ClientThreadArgs;

static void *server_accept_thread(void *arg);
static void *server_client_thread(void *arg);

static void server_handle_event(ServerContext *ctx, const GameEvent *event);
static void server_handle_player_join(ServerContext *ctx, int sender_socket, const EventPayload_PlayerJoin *payload);
static void server_handle_user_action(ServerContext *ctx, const EventPayload_UserAction *payload);
static void server_handle_disconnect(ServerContext *ctx, int socket_fd);

static void server_broadcast_event(ServerContext *ctx, const GameEvent *event);
static void server_send_event_to(ServerContext *ctx, int player_id, const GameEvent *event);
static void server_broadcast_state(ServerContext *ctx);
static void server_send_state_to(ServerContext *ctx, int viewer_id);

static PlayerState *server_get_player(ServerContext *ctx, int player_id);
static int server_find_open_slot(ServerContext *ctx);
static int server_find_player_by_socket(ServerContext *ctx, int socket_fd);
static void server_reset_player(PlayerState *player, int player_id, const char *name);
static void server_refresh_player_count(ServerContext *ctx);

static void server_start_match(ServerContext *ctx);
static void server_stop_match(ServerContext *ctx, const char *reason);
static void server_emit_turn_event(ServerContext *ctx, EventType type, int turn_number, int current_id, int next_id, int ms_remaining);
static void server_advance_turn(ServerContext *ctx);
static int server_next_active_player(ServerContext *ctx, int start_after);

static void server_emit_full_defense_event(ServerContext *ctx, int player_id);
static void server_emit_threshold_event(ServerContext *ctx, int player_id, int current_total);
static int to_coarse_percent(int current, int max);
static int clamp_int(int value, int min, int max);

ServerContext *server_create()
{
    ServerContext *ctx = (ServerContext *)malloc(sizeof(ServerContext));
    if (!ctx)
        return NULL;

    memset(ctx, 0, sizeof(ServerContext));
    ctx->max_players = MAX_PLAYERS;
    ctx->server_socket = -1;
    ctx->running = 0;
    ctx->accept_thread = 0;
    for (int i = 0; i < MAX_PLAYERS; ++i)
    {
        ctx->player_sockets[i] = -1;
    }
    pthread_mutex_init(&ctx->state_mutex, NULL);
    return ctx;
}

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

int server_init(ServerContext *ctx, int max_players)
{
    if (!ctx)
        return -1;

    int clamped_max = max_players > 0 && max_players <= MAX_PLAYERS ? max_players : MAX_PLAYERS;
    ctx->max_players = clamped_max;

    pthread_mutex_lock(&ctx->state_mutex);
    memset(&ctx->game_state, 0, sizeof(GameState));
    ctx->game_state.turn.ms_per_turn = TURN_DURATION_SECONDS * 1000;
    ctx->game_state.turn.current_player_id = -1;
    ctx->game_state.turn.turn_started_at = 0;
    ctx->game_state.turn.turn_number = 0;
    pthread_mutex_unlock(&ctx->state_mutex);

    server_main_init(ctx);
    server_main_on_initialized(ctx, clamped_max);
    return 0;
}

void server_start(ServerContext *ctx)
{
    if (!ctx)
        return;

    server_main_on_starting(ctx, DEFAULT_PORT);
    ctx->server_socket = net_create_server_socket(DEFAULT_PORT);
    if (ctx->server_socket < 0)
    {
        server_main_on_start_failed(ctx, "Failed to create socket");
        return;
    }

    ctx->running = 1;
    if (pthread_create(&ctx->accept_thread, NULL, server_accept_thread, ctx) != 0)
    {
        server_main_on_accept_thread_failed(ctx, "Failed to create accept thread");
        ctx->running = 0;
        net_close_socket(ctx->server_socket);
        ctx->server_socket = -1;
    }
    else
    {
        server_main_on_started(ctx, DEFAULT_PORT);
    }
}

void server_stop(ServerContext *ctx)
{
    if (!ctx)
        return;

    server_main_on_stopping(ctx);
    ctx->running = 0;

    if (ctx->server_socket >= 0)
    {
        net_close_socket(ctx->server_socket);
        ctx->server_socket = -1;
    }

    if (ctx->accept_thread)
    {
        pthread_join(ctx->accept_thread, NULL);
        ctx->accept_thread = 0;
    }

    pthread_mutex_lock(&ctx->state_mutex);
    for (int i = 0; i < MAX_PLAYERS; ++i)
    {
        if (ctx->player_sockets[i] != -1)
        {
            net_close_socket(ctx->player_sockets[i]);
            ctx->player_sockets[i] = -1;
            ctx->game_state.players[i].is_active = 0;
            ctx->game_state.players[i].is_connected = 0;
        }
    }
    ctx->game_state.player_count = 0;
    ctx->game_state.match_started = 0;
    ctx->game_state.turn.current_player_id = -1;
    pthread_mutex_unlock(&ctx->state_mutex);
}

static void *server_accept_thread(void *arg)
{
    ServerContext *ctx = (ServerContext *)arg;
    struct sockaddr_in address;
    int addrlen = sizeof(address);

    server_main_on_accept_thread_started(ctx);

    while (ctx->running)
    {
        int new_socket = accept(ctx->server_socket, (struct sockaddr *)&address, (socklen_t *)&addrlen);
        if (new_socket < 0)
        {
            if (ctx->running)
                perror("accept");
            continue;
        }

        server_main_on_client_connected(ctx, new_socket);

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
            server_main_on_accept_thread_failed(ctx, "Failed to create client thread");
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
            server_main_on_client_disconnected(ctx, sock);
            break;
        }

        // If it's a join request, we need to pass the socket info
        if (event.type == EVENT_PLAYER_JOIN_REQUEST)
        {
            event.sender_id = sock; // HACK: pass socket as sender_id for join
        }

        server_handle_event(ctx, &event);
    }

    // Cleanup
    net_close_socket(sock);

    // Remove from player_sockets if present
    server_handle_disconnect(ctx, sock);

    return NULL;
}

static void server_handle_event(ServerContext *ctx, const GameEvent *event)
{
    if (!event)
        return;

    switch (event->type)
    {
    case EVENT_PLAYER_JOIN_REQUEST:
        server_handle_player_join(ctx, event->sender_id, &event->data.join_req);
        break;
    case EVENT_USER_ACTION:
        server_handle_user_action(ctx, &event->data.action);
        break;
    default:
        server_main_on_unhandled_event(ctx, event->type);
        break;
    }
}

static void server_handle_player_join(ServerContext *ctx, int sender_socket, const EventPayload_PlayerJoin *payload)
{
    if (!payload)
        return;

    GameEvent ack_event;
    memset(&ack_event, 0, sizeof(GameEvent));
    ack_event.type = EVENT_PLAYER_JOIN_ACK;
    ack_event.timestamp = time(NULL);
    ack_event.data.join_ack.success = 0;
    ack_event.data.join_ack.player_id = -1;

    pthread_mutex_lock(&ctx->state_mutex);
    int slot = server_find_open_slot(ctx);
    if (slot == -1)
    {
        snprintf(ack_event.data.join_ack.message, sizeof(ack_event.data.join_ack.message), "Server full");
    }
    else
    {
        server_reset_player(&ctx->game_state.players[slot], slot, payload->player_name);
        ctx->player_sockets[slot] = sender_socket;
        server_refresh_player_count(ctx);
        ack_event.data.join_ack.success = 1;
        ack_event.data.join_ack.player_id = slot;
        snprintf(ack_event.data.join_ack.message, sizeof(ack_event.data.join_ack.message), "Welcome!");
    }
    pthread_mutex_unlock(&ctx->state_mutex);

    if (!ack_event.data.join_ack.success)
    {
        net_send_event(sender_socket, &ack_event);
        return;
    }

    server_send_event_to(ctx, ack_event.data.join_ack.player_id, &ack_event);

    GameEvent lifecycle;
    memset(&lifecycle, 0, sizeof(GameEvent));
    lifecycle.type = EVENT_PLAYER_JOINED;
    lifecycle.timestamp = time(NULL);
    lifecycle.data.player_event.player_id = ack_event.data.join_ack.player_id;
    strncpy(lifecycle.data.player_event.player_name, payload->player_name, MAX_NAME_LEN - 1);
    server_broadcast_event(ctx, &lifecycle);

    pthread_mutex_lock(&ctx->state_mutex);
    int should_start = (!ctx->game_state.match_started && ctx->game_state.player_count >= MIN_PLAYERS);
    pthread_mutex_unlock(&ctx->state_mutex);

    server_broadcast_state(ctx);

    if (should_start)
    {
        server_start_match(ctx);
    }
}

static void server_handle_user_action(ServerContext *ctx, const EventPayload_UserAction *payload)
{
    if (!payload)
        return;

    int emit_turn_completed = 0;
    EventPayload_TurnInfo turn_info = {0};
    int emit_full_defense = 0;
    int defense_player_id = -1;
    int emit_threshold = 0;
    int threshold_player_id = -1;
    int threshold_total = 0;

    pthread_mutex_lock(&ctx->state_mutex);
    PlayerState *player = server_get_player(ctx, payload->player_id);
    if (!player || !player->is_active)
    {
        pthread_mutex_unlock(&ctx->state_mutex);
        return;
    }

    switch (payload->action_type)
    {
    case USER_ACTION_END_TURN:
        if (ctx->game_state.turn.current_player_id == player->player_id)
        {
            emit_turn_completed = 1;
            turn_info.current_player_id = player->player_id;
            turn_info.next_player_id = server_next_active_player(ctx, player->player_id);
            turn_info.turn_number = ctx->game_state.turn.turn_number;
            turn_info.ms_remaining = 0;
        }
        break;
    case USER_ACTION_SET_DEFENSE:
        player->is_defending = payload->value;
        if (payload->value >= 100)
        {
            player->stars = 0;
            player->has_crossed_threshold = 0;
            emit_full_defense = 1;
            defense_player_id = player->player_id;
        }
        break;
    case USER_ACTION_UPGRADE_PLANET:
        player->planet.level += 1;
        break;
    case USER_ACTION_UPGRADE_SHIP:
        player->ship.level += 1;
        break;
    case USER_ACTION_REPAIR_PLANET:
        player->planet.current_health = clamp_int(player->planet.current_health + payload->value, 0, player->planet.max_health);
        break;
    case USER_ACTION_ATTACK_PLANET:
        if (payload->target_player_id >= 0)
        {
            PlayerState *target = server_get_player(ctx, payload->target_player_id);
            if (target)
            {
                target->planet.current_health = clamp_int(target->planet.current_health - payload->value, 0, target->planet.max_health);
            }
        }
        break;
    default:
        server_main_on_unknown_action(ctx, payload->action_type, payload->player_id);
        break;
    }

    if (player->stars >= STAR_WARNING_THRESHOLD && !player->has_crossed_threshold)
    {
        player->has_crossed_threshold = 1;
        emit_threshold = 1;
        threshold_player_id = player->player_id;
        threshold_total = player->stars;
    }

    pthread_mutex_unlock(&ctx->state_mutex);

    if (emit_turn_completed)
    {
        server_emit_turn_event(ctx, EVENT_TURN_COMPLETED, turn_info.turn_number, turn_info.current_player_id, turn_info.next_player_id, turn_info.ms_remaining);
        server_advance_turn(ctx);
    }

    if (emit_full_defense)
    {
        server_emit_full_defense_event(ctx, defense_player_id);
    }

    if (emit_threshold)
    {
        server_emit_threshold_event(ctx, threshold_player_id, threshold_total);
    }

    server_broadcast_state(ctx);
}

static void server_handle_disconnect(ServerContext *ctx, int socket_fd)
{
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
    ctx->player_sockets[player_id] = -1;
    server_refresh_player_count(ctx);

    int was_current = (ctx->game_state.turn.current_player_id == player_id);
    pthread_mutex_unlock(&ctx->state_mutex);

    GameEvent lifecycle;
    memset(&lifecycle, 0, sizeof(GameEvent));
    lifecycle.type = EVENT_PLAYER_LEFT;
    lifecycle.timestamp = time(NULL);
    lifecycle.data.player_event.player_id = player_id;
    strncpy(lifecycle.data.player_event.player_name, name_copy, MAX_NAME_LEN - 1);
    server_broadcast_event(ctx, &lifecycle);

    if (was_current)
    {
        server_advance_turn(ctx);
    }

    pthread_mutex_lock(&ctx->state_mutex);
    int player_count = ctx->game_state.player_count;
    int match_running = ctx->game_state.match_started;
    pthread_mutex_unlock(&ctx->state_mutex);

    if (match_running && player_count < MIN_PLAYERS)
    {
        server_stop_match(ctx, "Not enough players");
    }

    server_broadcast_state(ctx);
}

static void server_broadcast_event(ServerContext *ctx, const GameEvent *event)
{
    pthread_mutex_lock(&ctx->state_mutex);
    for (int i = 0; i < MAX_PLAYERS; ++i)
    {
        int sock = ctx->player_sockets[i];
        if (sock != -1)
        {
            net_send_event(sock, event);
        }
    }
    pthread_mutex_unlock(&ctx->state_mutex);
}

static void server_send_event_to(ServerContext *ctx, int player_id, const GameEvent *event)
{
    pthread_mutex_lock(&ctx->state_mutex);
    if (player_id >= 0 && player_id < MAX_PLAYERS)
    {
        int sock = ctx->player_sockets[player_id];
        if (sock != -1)
        {
            net_send_event(sock, event);
        }
    }
    pthread_mutex_unlock(&ctx->state_mutex);
}

static void server_send_state_to(ServerContext *ctx, int viewer_id)
{
    PlayerGameState player_state;
    memset(&player_state, 0, sizeof(PlayerGameState));

    pthread_mutex_lock(&ctx->state_mutex);
    PlayerState *viewer = server_get_player(ctx, viewer_id);
    if (!viewer || !viewer->is_active)
    {
        pthread_mutex_unlock(&ctx->state_mutex);
        return;
    }

    player_state.viewer_id = viewer_id;
    player_state.self = *viewer;

    for (int i = 0; i < MAX_PLAYERS; ++i)
    {
        PlayerPublicInfo *info = &player_state.entries[i];
        PlayerState *candidate = &ctx->game_state.players[i];
        info->player_id = i;
        info->planet_level = candidate->planet.level;
        info->ship_level = candidate->ship.level;
        info->ship_base_damage = candidate->ship.base_damage;
        if (candidate->is_active)
        {
            if (viewer_id == i)
            {
                info->show_stars = 1;
                info->stars = candidate->stars;
                info->coarse_planet_health = (candidate->planet.max_health == 0) ? 0 : (candidate->planet.current_health * 100) / candidate->planet.max_health;
                info->coarse_ship_health = (candidate->ship.max_health == 0) ? 0 : (candidate->ship.current_health * 100) / candidate->ship.max_health;
            }
            else
            {
                info->show_stars = candidate->stars >= STAR_WARNING_THRESHOLD;
                info->stars = info->show_stars ? candidate->stars : -1;
                info->coarse_planet_health = to_coarse_percent(candidate->planet.current_health, candidate->planet.max_health);
                info->coarse_ship_health = to_coarse_percent(candidate->ship.current_health, candidate->ship.max_health);
            }
        }
        else
        {
            info->show_stars = 0;
            info->stars = -1;
            info->coarse_planet_health = 0;
            info->coarse_ship_health = 0;
        }
    }
    pthread_mutex_unlock(&ctx->state_mutex);

    GameEvent state_event;
    memset(&state_event, 0, sizeof(GameEvent));
    state_event.type = EVENT_STATE_UPDATE;
    state_event.timestamp = time(NULL);
    state_event.data.state_update.game = player_state;

    server_send_event_to(ctx, viewer_id, &state_event);
}

static void server_broadcast_state(ServerContext *ctx)
{
    pthread_mutex_lock(&ctx->state_mutex);
    int active_ids[MAX_PLAYERS];
    int count = 0;
    for (int i = 0; i < MAX_PLAYERS; ++i)
    {
        if (ctx->game_state.players[i].is_active)
        {
            active_ids[count++] = i;
        }
    }
    pthread_mutex_unlock(&ctx->state_mutex);

    for (int i = 0; i < count; ++i)
    {
        server_send_state_to(ctx, active_ids[i]);
    }
}

static PlayerState *server_get_player(ServerContext *ctx, int player_id)
{
    if (player_id < 0 || player_id >= MAX_PLAYERS)
        return NULL;
    return &ctx->game_state.players[player_id];
}

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

static int server_find_player_by_socket(ServerContext *ctx, int socket_fd)
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
    player->ship.max_health = STARTING_SHIP_MAX_HEALTH;
    player->ship.current_health = STARTING_SHIP_MAX_HEALTH;
    player->ship.base_damage = STARTING_SHIP_BASE_DAMAGE;
    player->is_defending = 0;
    player->has_crossed_threshold = 0;
}

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

static void server_start_match(ServerContext *ctx)
{
    int current_player;
    int next_player;
    int turn_number;
    int ms_per_turn;

    pthread_mutex_lock(&ctx->state_mutex);
    if (ctx->game_state.player_count < MIN_PLAYERS)
    {
        pthread_mutex_unlock(&ctx->state_mutex);
        return;
    }
    ctx->game_state.match_started = 1;
    ctx->game_state.turn.turn_number = 1;
    ctx->game_state.turn.current_player_id = server_next_active_player(ctx, -1);
    ctx->game_state.turn.turn_started_at = time(NULL);
    current_player = ctx->game_state.turn.current_player_id;
    turn_number = ctx->game_state.turn.turn_number;
    ms_per_turn = ctx->game_state.turn.ms_per_turn;
    next_player = server_next_active_player(ctx, current_player);
    pthread_mutex_unlock(&ctx->state_mutex);

    if (current_player == -1)
    {
        return;
    }

    GameEvent start_event;
    memset(&start_event, 0, sizeof(GameEvent));
    start_event.type = EVENT_MATCH_START;
    start_event.timestamp = time(NULL);
    server_broadcast_event(ctx, &start_event);

    server_emit_turn_event(ctx, EVENT_TURN_STARTED, turn_number, current_player, next_player, ms_per_turn);
}

static void server_stop_match(ServerContext *ctx, const char *reason)
{
    pthread_mutex_lock(&ctx->state_mutex);
    ctx->game_state.match_started = 0;
    ctx->game_state.turn.current_player_id = -1;
    pthread_mutex_unlock(&ctx->state_mutex);

    GameEvent stop_event;
    memset(&stop_event, 0, sizeof(GameEvent));
    stop_event.type = EVENT_MATCH_STOP;
    stop_event.timestamp = time(NULL);
    stop_event.data.error.error_code = 0;
    if (reason)
    {
        strncpy(stop_event.data.error.message, reason, sizeof(stop_event.data.error.message) - 1);
    }
    server_broadcast_event(ctx, &stop_event);
}

static void server_emit_turn_event(ServerContext *ctx, EventType type, int turn_number, int current_id, int next_id, int ms_remaining)
{
    GameEvent event;
    memset(&event, 0, sizeof(GameEvent));
    event.type = type;
    event.timestamp = time(NULL);
    event.data.turn.current_player_id = current_id;
    event.data.turn.next_player_id = next_id;
    event.data.turn.turn_number = turn_number;
    event.data.turn.ms_remaining = ms_remaining;
    server_broadcast_event(ctx, &event);
}

static void server_advance_turn(ServerContext *ctx)
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

    ctx->game_state.turn.current_player_id = next_player;
    ctx->game_state.turn.turn_number += 1;
    ctx->game_state.turn.turn_started_at = time(NULL);
    int current_turn = ctx->game_state.turn.current_player_id;
    int ms_per_turn = ctx->game_state.turn.ms_per_turn;
    int turn_number = ctx->game_state.turn.turn_number;
    int following = server_next_active_player(ctx, current_turn);
    pthread_mutex_unlock(&ctx->state_mutex);
    server_emit_turn_event(ctx, EVENT_TURN_STARTED, turn_number, current_turn, following, ms_per_turn);
}

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

static void server_emit_full_defense_event(ServerContext *ctx, int player_id)
{
    GameEvent defense_event;
    memset(&defense_event, 0, sizeof(GameEvent));
    defense_event.type = EVENT_DEFENSE_FULL;
    defense_event.timestamp = time(NULL);
    defense_event.data.defense.defender_id = player_id;
    defense_event.data.defense.was_full_defense = 1;
    defense_event.data.defense.stars_reset = 1;
    server_broadcast_event(ctx, &defense_event);
}

static void server_emit_threshold_event(ServerContext *ctx, int player_id, int current_total)
{
    GameEvent threshold;
    memset(&threshold, 0, sizeof(GameEvent));
    threshold.type = EVENT_STAR_THRESHOLD_REACHED;
    threshold.timestamp = time(NULL);
    threshold.data.threshold.player_id = player_id;
    threshold.data.threshold.threshold = STAR_WARNING_THRESHOLD;
    threshold.data.threshold.current_total = current_total;
    server_broadcast_event(ctx, &threshold);
}

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

static int clamp_int(int value, int min, int max)
{
    if (value < min)
        return min;
    if (value > max)
        return max;
    return value;
}
