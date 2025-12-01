#ifndef SERVER_MAIN_H
#define SERVER_MAIN_H

#include "../server/server_api.h"

// Arguments passed to each client thread
typedef struct
{
    ServerContext *ctx;
    net_socket_t socket_fd;
} ClientThreadArgs;

// Server callback functions
int server_on_init(ServerContext *ctx);
void server_on_initialized(ServerContext *ctx, int max_players);
void server_on_starting(ServerContext *ctx, int port);
void server_on_start_failed(ServerContext *ctx, const char *message);
void server_on_started(ServerContext *ctx, int port);
void server_on_accept_thread_started(ServerContext *ctx);
void server_on_accept_thread_failed(ServerContext *ctx, const char *message);
void server_on_stopping(ServerContext *ctx);
void server_on_client_connected(ServerContext *ctx, net_socket_t socket_fd);
void server_on_client_disconnected(ServerContext *ctx, net_socket_t socket_fd);
void server_on_unhandled_event(ServerContext *ctx, EventType type);
void server_on_unknown_action(ServerContext *ctx, UserActionType action, int player_id);

// Thread entry points
static void *server_accept_thread(void *arg);
static void *server_client_thread(void *arg);

// Event handlers
static void server_handle_event(ServerContext *ctx, net_socket_t sender_socket, const GameEvent *event);
static void server_handle_player_join(ServerContext *ctx, net_socket_t sender_socket, const EventPayload_PlayerJoin *payload);
static void server_handle_user_action(ServerContext *ctx, const EventPayload_UserAction *payload);
static void server_handle_match_start_request(ServerContext *ctx, int requester_id);
static void server_handle_disconnect(ServerContext *ctx, net_socket_t socket_fd);
void server_on_turn_action(ServerContext *ctx, const EventPayload_UserAction *action);

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
static void server_emit_turn_event(ServerContext *ctx, EventType type, int turn_number, int current_id, int next_id, int is_match_start, const EventPayload_UserAction *last_action, int threshold_player_id);
static void server_advance_turn(ServerContext *ctx, const EventPayload_UserAction *last_action);
static int server_next_active_player(ServerContext *ctx, int start_after);
static int server_compute_valid_actions(ServerContext *ctx, int player_id, int current_player_id);

// Cost and stat calculation helpers
static int server_get_planet_upgrade_cost(int current_level);
static int server_get_ship_upgrade_cost(int current_level);
static int server_get_repair_cost(int planet_level);
static int server_get_planet_base_health(int level);
static int server_get_planet_base_income(int level);
static int server_get_ship_base_damage(int level);
static int server_star_gained_attacker(int level,int damage_dealt,int planet_max_health);

// Misc helpers
static void server_emit_host_update(ServerContext *ctx, int host_id, const char *host_name);
static int server_collect_active_players(ServerContext *ctx, int *out_ids, int max_ids);
static int server_build_player_snapshot(ServerContext *ctx, int viewer_id, PlayerGameState *out_state);
static int to_coarse_percent(int current, int max);
static int server_select_host_locked(ServerContext *ctx);
static void server_send_error_event(ServerContext *ctx, int player_id, int error_code, const char *message);
static int server_start_discovery_service(ServerContext *ctx);
static void server_stop_discovery_service(ServerContext *ctx);
static void *server_discovery_thread(void *arg);

#endif // SERVER_MAIN_H
