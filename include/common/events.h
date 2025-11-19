#ifndef EVENTS_H
#define EVENTS_H

#include "game_types.h"
#include <time.h>

typedef enum
{
    EVENT_UNKNOWN = 0,
    EVENT_PLAYER_JOIN_REQUEST,
    EVENT_PLAYER_JOIN_ACK,
    EVENT_MATCH_START,
    EVENT_USER_ACTION,
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
    int x;
    int y;
    int action_type; // TODO: 0: move, 1: attack, etc. Make this an enum
} EventPayload_UserAction;

typedef struct
{
    GameState game;
} EventPayload_StateUpdate;

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
        EventPayload_UserAction action;
        EventPayload_StateUpdate state_update;
        EventPayload_GameOver game_over;
        EventPayload_Error error;
    } data;
} GameEvent;

// Function pointer type for event handling
typedef void (*EventHandler)(const GameEvent *event, void *context);

#endif // EVENTS_H
