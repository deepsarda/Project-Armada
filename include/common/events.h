#ifndef EVENTS_H
#define EVENTS_H

#include "game_types.h"
#include <time.h>

typedef enum
{
    EVENT_UNKNOWN = 0,
    EVENT_PLAYER_JOIN_REQUEST,
    EVENT_PLAYER_JOIN_ACK,
    // Player lifecycle
    EVENT_PLAYER_JOINED,
    EVENT_PLAYER_LEFT,
    // Match lifecycle
    EVENT_MATCH_START,
    EVENT_MATCH_STOP,
    // Turn loop notifications
    EVENT_TURN_STARTED,
    EVENT_TURN_COMPLETED,
    EVENT_TURN_TIMEOUT,
    // Client issued commands routed via server
    EVENT_USER_ACTION,
    // Gameplay feedback hooks
    EVENT_PLANET_UPGRADED,
    EVENT_SHIP_UPGRADED,
    EVENT_PLANET_ATTACKED,
    EVENT_PLANET_REPAIRED,
    EVENT_STARS_CHANGED,
    EVENT_DEFENSE_FULL,
    EVENT_STAR_THRESHOLD_REACHED,
    EVENT_STATE_UPDATE,
    EVENT_GAME_OVER,
    EVENT_ERROR
} EventType;

// Payload structures for specific events

typedef struct
{
    char player_name[32];
} EventPayload_PlayerJoin;

typedef struct
{
    int player_id;
    int success;
    char message[64];
} EventPayload_JoinAck;

typedef struct
{
    int player_id;
    UserActionType action_type;
    int target_player_id;
    int value;
    int metadata;
} EventPayload_UserAction;

typedef struct
{
    PlayerGameState game;
} EventPayload_StateUpdate;

typedef struct
{
    int player_id;
    char player_name[MAX_NAME_LEN];
    int reason_code;
} EventPayload_PlayerLifecycle;

typedef struct
{
    int current_player_id;
    int next_player_id;
    int turn_number;
    int ms_remaining;
} EventPayload_TurnInfo;

typedef struct
{
    int player_id;
    int from_level;
    int new_level;
    int resource_delta;
} EventPayload_Upgrade;

typedef struct
{
    int attacker_id;
    int defender_id;
    int planet_damage;
    int star_delta;
    int was_planet_destroyed;
} EventPayload_Attack;

typedef struct
{
    int player_id;
    int repaired_amount;
    int resulting_health;
} EventPayload_Repair;

typedef struct
{
    int player_id;
    int delta;
    int new_total;
    int reason_code;
} EventPayload_StarsChange;

typedef struct
{
    int defender_id;
    int was_full_defense;
    int stars_reset;
} EventPayload_Defense;

typedef struct
{
    int player_id;
    int threshold;
    int current_total;
} EventPayload_Threshold;

typedef struct
{
    int winner_id;
    char reason[64];
} EventPayload_GameOver;

typedef struct
{
    int error_code;
    char message[128];
} EventPayload_Error;

// Main Event Structure
typedef struct
{
    EventType type;
    int sender_id; // -1 for server, 0-3 for players
    time_t timestamp;

    union
    {
        EventPayload_PlayerJoin join_req;
        EventPayload_JoinAck join_ack;
        EventPayload_PlayerLifecycle player_event;
        EventPayload_TurnInfo turn;
        EventPayload_UserAction action;
        EventPayload_Upgrade upgrade;
        EventPayload_Attack attack;
        EventPayload_Repair repair;
        EventPayload_StarsChange stars_change;
        EventPayload_Defense defense;
        EventPayload_Threshold threshold;
        EventPayload_StateUpdate state_update;
        EventPayload_GameOver game_over;
        EventPayload_Error error;
    } data;
} GameEvent;

// Function pointer type for event handling
typedef void (*EventHandler)(const GameEvent *event, void *context);

#endif // EVENTS_H
