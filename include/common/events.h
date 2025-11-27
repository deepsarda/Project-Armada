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
    EVENT_HOST_UPDATED,
    EVENT_MATCH_START_REQUEST,
    EVENT_MATCH_START,
    // Turn loop notifications
    EVENT_TURN_STARTED,
    // Client issued commands routed via server
    EVENT_USER_ACTION,
    // Gameplay feedback hooks
    EVENT_STAR_THRESHOLD_REACHED,
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
    int host_player_id;
    int is_host;
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
    int player_id;
    char player_name[MAX_NAME_LEN];
    int reason_code;
} EventPayload_PlayerLifecycle;

// Bitmask for valid actions
#define VALID_ACTION_END_TURN (1 << 0)
#define VALID_ACTION_ATTACK_PLANET (1 << 1)
#define VALID_ACTION_REPAIR_PLANET (1 << 2)
#define VALID_ACTION_UPGRADE_PLANET (1 << 3)
#define VALID_ACTION_UPGRADE_SHIP (1 << 4)

typedef struct
{
    int current_player_id;
    int next_player_id;
    int turn_number;
    int is_match_start;
    int valid_actions; // Bitmask of valid actions for the receiving player
    EventPayload_UserAction last_action;
    PlayerGameState game;
} EventPayload_TurnInfo;

typedef struct
{
    GameState state;
} EventPayload_MatchStart;

typedef struct
{
    int host_player_id;
    char host_player_name[MAX_NAME_LEN];
} EventPayload_HostUpdate;

typedef struct
{
    int player_id;
    int threshold;
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
        EventPayload_MatchStart match_start;
        EventPayload_HostUpdate host_update;
        EventPayload_Threshold threshold;
        EventPayload_GameOver game_over;
        EventPayload_Error error;
    } data;
} GameEvent;

// Function pointer type for event handling
typedef void (*EventHandler)(const GameEvent *event, void *context);

#endif // EVENTS_H
