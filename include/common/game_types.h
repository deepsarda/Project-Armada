#ifndef GAME_TYPES_H
#define GAME_TYPES_H

#include <stdint.h>

#define MAX_PLAYERS 4
#define MIN_PLAYERS 2

typedef struct
{
    int id;
    char name[32];
    int is_active;
} Player;

typedef struct
{
    Player players[MAX_PLAYERS];
    int player_count;
    int match_started;
    int current_turn_player_id;
    int is_game_over;
    int winner_id;
} GameState;

#endif // GAME_TYPES_H
