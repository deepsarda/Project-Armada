#ifndef GAME_TYPES_H
#define GAME_TYPES_H

#include <stdint.h>
#include <time.h>

#define MAX_PLAYERS 4
#define MIN_PLAYERS 2
#define MAX_NAME_LEN 32

// Economy milestones
#define STAR_GOAL 1000
#define STAR_WARNING_THRESHOLD 900
#define STARTING_STARS 100

#define STARTING_PLANET_LEVEL 1
#define STARTING_PLANET_MAX_HEALTH 1000
#define STARTING_PLANET_INCOME 25

#define STARTING_SHIP_LEVEL 1
#define STARTING_SHIP_MAX_HEALTH 100
#define STARTING_SHIP_BASE_DAMAGE 15

#define TURN_DURATION_SECONDS 30

typedef enum
{
    USER_ACTION_NONE = 0,
    USER_ACTION_END_TURN,
    USER_ACTION_ATTACK_PLANET,
    USER_ACTION_REPAIR_PLANET,
    USER_ACTION_UPGRADE_PLANET,
    USER_ACTION_UPGRADE_SHIP,
    USER_ACTION_SET_DEFENSE
} UserActionType;

typedef enum
{
    HEALTH_VISIBILITY_0 = 0,
    HEALTH_VISIBILITY_25 = 25,
    HEALTH_VISIBILITY_50 = 50,
    HEALTH_VISIBILITY_75 = 75,
    HEALTH_VISIBILITY_100 = 100
} HealthVisibilityLevel;

typedef struct
{
    int level;
    int max_health;
    int current_health;
    int base_income;
    int upgrade_cost;
} PlanetStats;

typedef struct
{
    int level;
    int max_health;
    int current_health;
    int base_damage;
    int upgrade_cost;
} ShipStats;

typedef struct
{
    int player_id;
    char name[MAX_NAME_LEN];
    int is_active;
    int is_connected;
    int stars;
    int is_defending;
    int has_crossed_threshold;
    PlanetStats planet;
    ShipStats ship;
} PlayerState;

typedef struct
{
    int turn_number;
    int current_player_id;
    int ms_per_turn;
    time_t turn_started_at;
} TurnState;

typedef struct
{
    int player_id;
    int show_stars;
    int stars;
    int coarse_planet_health;
    int coarse_ship_health;
    int ship_level;
    int planet_level;
    int ship_base_damage;
} PlayerPublicInfo;

typedef struct
{
    int viewer_id;
    PlayerState self;
    PlayerPublicInfo entries[MAX_PLAYERS];
} PlayerGameState; // Limited-info snapshot tailored per player

typedef struct
{
    PlayerState players[MAX_PLAYERS];
    int player_count;
    int match_started;
    int is_game_over;
    int winner_id;
    TurnState turn;
} GameState;

#endif // GAME_TYPES_H
