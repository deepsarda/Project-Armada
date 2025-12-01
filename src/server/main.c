#include "../../include/server/main.h"
#include "../../include/client/ui_notifications.h"
#include <stdio.h>
#include <string.h>

// ANSI color codes for server logging
#define SRV_COLOR_RESET "\033[0m"
#define SRV_COLOR_GREEN "\033[32m"
#define SRV_COLOR_YELLOW "\033[33m"
#define SRV_COLOR_RED "\033[31m"
#define SRV_COLOR_CYAN "\033[36m"
#define SRV_COLOR_MAGENTA "\033[35m"
#define SRV_COLOR_BOLD "\033[1m"

int server_on_init(ServerContext *ctx)
{
    (void)ctx;
    return 0;
}

void server_on_initialized(ServerContext *ctx, int max_players)
{
    (void)ctx;
    armada_server_logf(SRV_COLOR_GREEN "[Server]" SRV_COLOR_RESET " Initialized for up to " SRV_COLOR_BOLD "%d" SRV_COLOR_RESET " players.", max_players);
}

void server_on_starting(ServerContext *ctx, int port)
{
    (void)ctx;
    armada_server_logf(SRV_COLOR_CYAN "[Server]" SRV_COLOR_RESET " Starting server on port " SRV_COLOR_BOLD "%d" SRV_COLOR_RESET "...", port);
}

void server_on_start_failed(ServerContext *ctx, const char *message)
{
    (void)ctx;
    armada_server_logf(SRV_COLOR_RED "[Server] ERROR:" SRV_COLOR_RESET " Failed to start: %s", message ? message : "unknown error");
}

void server_on_started(ServerContext *ctx, int port)
{
    (void)ctx;
    armada_server_logf(SRV_COLOR_GREEN "[Server]" SRV_COLOR_RESET " Server listening on port " SRV_COLOR_BOLD "%d" SRV_COLOR_RESET ".", port);
}

void server_on_accept_thread_started(ServerContext *ctx)
{
    (void)ctx;
    armada_server_logf(SRV_COLOR_GREEN "[Server]" SRV_COLOR_RESET " Accept thread running.");
}

void server_on_accept_thread_failed(ServerContext *ctx, const char *message)
{
    (void)ctx;
    armada_server_logf(SRV_COLOR_RED "[Server] ERROR:" SRV_COLOR_RESET " Accept thread failed: %s", message ? message : "unknown error");
}

void server_on_stopping(ServerContext *ctx)
{
    (void)ctx;
    armada_server_logf(SRV_COLOR_YELLOW "[Server]" SRV_COLOR_RESET " Stopping server...");
}

void server_on_client_connected(ServerContext *ctx, net_socket_t socket_fd)
{
    (void)ctx;
    armada_server_logf(SRV_COLOR_GREEN "[Server]" SRV_COLOR_RESET " Client connected on socket " SRV_COLOR_CYAN "%llu" SRV_COLOR_RESET ".", (unsigned long long)socket_fd);
}

void server_on_client_disconnected(ServerContext *ctx, net_socket_t socket_fd)
{
    (void)ctx;
    armada_server_logf(SRV_COLOR_YELLOW "[Server]" SRV_COLOR_RESET " Client disconnected from socket " SRV_COLOR_CYAN "%llu" SRV_COLOR_RESET ".", (unsigned long long)socket_fd);
}

void server_on_unhandled_event(ServerContext *ctx, EventType type)
{
    (void)ctx;
    armada_server_logf(SRV_COLOR_YELLOW "[Server]" SRV_COLOR_RESET " Unhandled event type " SRV_COLOR_MAGENTA "%d" SRV_COLOR_RESET ".", type);
}

void server_on_unknown_action(ServerContext *ctx, UserActionType action, int player_id)
{
    (void)ctx;
    armada_server_logf(SRV_COLOR_RED "[Server] WARNING:" SRV_COLOR_RESET " Unknown action " SRV_COLOR_MAGENTA "%d" SRV_COLOR_RESET " from player " SRV_COLOR_CYAN "%d" SRV_COLOR_RESET ".", action, player_id);
}

void server_on_turn_action(ServerContext *ctx, const EventPayload_UserAction *action)
{

    if (action)
    {
        // TODO: Enter logic to process the action here
        PlayerState *player = &ctx->game_state.players[action->player_id];
        switch (action->action_type)
        {
        case USER_ACTION_UPGRADE_PLANET:
            int cost = server_get_planet_upgrade_cost(player->planet.level);
            if (cost > player->stars)
            {
                armada_server_logf(SRV_COLOR_RED "[Server] ERROR:" SRV_COLOR_RESET " Player " SRV_COLOR_CYAN "%d" SRV_COLOR_RESET " attempted to upgrade planet without enough stars.", action->player_id);
                break;
            }
            player->stars -= cost;
            player->planet.level += 1;
            player->planet.max_health = server_get_planet_base_health(player->planet.level);
            player->planet.current_health = player->planet.max_health; // Heal to full on upgrade
            armada_server_logf(SRV_COLOR_GREEN "[Server]" SRV_COLOR_RESET " Player " SRV_COLOR_CYAN "%d" SRV_COLOR_RESET " upgraded their planet to level " SRV_COLOR_BOLD "%d" SRV_COLOR_RESET " for " SRV_COLOR_YELLOW "%d" SRV_COLOR_RESET " stars.",
                               action->player_id,
                               player->planet.level,
                               cost);
            break;
        case USER_ACTION_UPGRADE_SHIP:
            int ship_cost = server_get_ship_upgrade_cost(player->ship.level);
            if (ship_cost > player->stars)
            {
                armada_server_logf(SRV_COLOR_RED "[Server] ERROR:" SRV_COLOR_RESET " Player " SRV_COLOR_CYAN "%d" SRV_COLOR_RESET " attempted to upgrade ship without enough stars.", action->player_id);
                break;
            }
            player->stars -= ship_cost;
            player->ship.level += 1;
            player->ship.base_damage = server_get_ship_base_damage(player->ship.level);
            armada_server_logf(SRV_COLOR_GREEN "[Server]" SRV_COLOR_RESET " Player " SRV_COLOR_CYAN "%d" SRV_COLOR_RESET " upgraded their ship.",
                               action->player_id);
            break;
        case USER_ACTION_REPAIR_PLANET:
            int repair_cost = server_get_repair_cost(player->planet.level);
            if (repair_cost > player->stars)
            {
                armada_server_logf(SRV_COLOR_RED "[Server] ERROR:" SRV_COLOR_RESET " Player " SRV_COLOR_CYAN "%d" SRV_COLOR_RESET " attempted to repair planet without enough stars.", action->player_id);
                break;
            }
            player->stars -= repair_cost;
            player->planet.current_health = player->planet.max_health;
            armada_server_logf(SRV_COLOR_GREEN "[Server]" SRV_COLOR_RESET " Player " SRV_COLOR_CYAN "%d" SRV_COLOR_RESET " repaired their planet for " SRV_COLOR_YELLOW "%d" SRV_COLOR_RESET " stars.",
                               action->player_id,
                               repair_cost);
            break;
        case USER_ACTION_ATTACK_PLANET:
        {
            int target_id = action->target_player_id;
            if (target_id < 0 || target_id >= MAX_PLAYERS || !ctx->game_state.players[target_id].is_active)
            {
                armada_server_logf(SRV_COLOR_RED "[Server] ERROR:" SRV_COLOR_RESET " Player " SRV_COLOR_CYAN "%d" SRV_COLOR_RESET " attempted to attack invalid target " SRV_COLOR_CYAN "%d" SRV_COLOR_RESET ".",
                                   action->player_id,
                                   target_id);
                break;
            }
            PlayerState *target_player = &ctx->game_state.players[target_id];
            int damage = player->ship.base_damage;
            target_player->planet.current_health -= damage;
            if (target_player->planet.current_health <= 0)
            {
                target_player->planet.current_health = 0;
                target_player->stars = 0; // Eliminated player loses all stars
                armada_server_logf(SRV_COLOR_GREEN "[Server]" SRV_COLOR_RESET " Player " SRV_COLOR_CYAN "%d" SRV_COLOR_RESET " has lost all their stars due to planet destruction.",
                                   target_id);
            }
            // Calculate stars gained by attacker
            int stars_gained = server_get_attack_star_gain(target_player->planet.level, damage, target_player->planet.max_health);
            player->stars += stars_gained;
            armada_server_logf(SRV_COLOR_GREEN "[Server]" SRV_COLOR_RESET " Player " SRV_COLOR_CYAN "%d" SRV_COLOR_RESET " attacked player " SRV_COLOR_CYAN "%d" SRV_COLOR_RESET "'s planet for " SRV_COLOR_YELLOW "%d" SRV_COLOR_RESET " damage, gaining " SRV_COLOR_YELLOW "%d" SRV_COLOR_RESET " stars.",
                               action->player_id,
                               target_id,
                               damage,
                               stars_gained);
        }
        break;

        default:
            break;
        }

        armada_server_logf(SRV_COLOR_RED "[Server] WARNING:" SRV_COLOR_RESET " Processing action " SRV_COLOR_MAGENTA "%d" SRV_COLOR_RESET " from player " SRV_COLOR_CYAN "%d" SRV_COLOR_RESET " targeting " SRV_COLOR_CYAN "%d" SRV_COLOR_RESET " (value=%d meta=%d).",
                           action->action_type,
                           action->player_id,
                           action->target_player_id,
                           action->value,
                           action->metadata);
    }
}