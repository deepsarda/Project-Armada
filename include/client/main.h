#ifndef CLIENT_MAIN_H
#define CLIENT_MAIN_H

#include "../client/client_api.h"

int client_main_init(ClientContext *ctx, const char *player_name);
void client_main_on_connecting(ClientContext *ctx, const char *server_addr, int port);
void client_main_on_connected(ClientContext *ctx);
void client_main_on_connection_failed(ClientContext *ctx, const char *server_addr, int port);
void client_main_on_disconnected(ClientContext *ctx);
void client_main_on_join_request(ClientContext *ctx);
void client_main_on_join_ack(ClientContext *ctx, const EventPayload_JoinAck *payload);
void client_main_on_player_joined(ClientContext *ctx, const EventPayload_PlayerLifecycle *payload);
void client_main_on_player_left(ClientContext *ctx, const EventPayload_PlayerLifecycle *payload);
void client_main_on_host_update(ClientContext *ctx, const EventPayload_HostUpdate *payload);
void client_main_on_match_start(ClientContext *ctx, const EventPayload_MatchStart *payload);
void client_main_on_match_stop(ClientContext *ctx, const EventPayload_Error *payload);
void client_main_on_turn_event(ClientContext *ctx, EventType type, const EventPayload_TurnInfo *payload);
void client_main_on_threshold(ClientContext *ctx, const EventPayload_Threshold *payload);
void client_main_on_action_sent(ClientContext *ctx, UserActionType type, int target_player_id, int value, int metadata);
void client_main_on_game_over(ClientContext *ctx, int winner_id);

#endif // CLIENT_MAIN_H
