//
// save_convert.h
//
// Heretic 2 Save File Converter - 32-bit to 64-bit
//
// This tool converts save files created by the 32-bit build to be
// compatible with the 64-bit build.
//

#pragma once

#include <stdint.h>
#include <stdio.h>
#include <stdbool.h>
#include <string.h>
#include <stdlib.h>

//=============================================================================
// Constants (must match game definitions)
//=============================================================================

#define MAX_QPATH           64
#define MAX_INFO_STRING     512
#define MAX_FM_MESH_NODES   16
#define MAX_BUOY_BRANCHES   3
#define MAX_MAP_BUOYS       256
#define MAX_ALERT_ENTS      1024
#define MAX_CLIENTS         8
#define SAVE_VERSION        "H2RSG1"

//=============================================================================
// Known struct sizes
//=============================================================================

// x86 (32-bit) sizes
#define X86_EDICT_SIZE          1680
#define X86_GCLIENT_SIZE        4380
#define X86_GAME_LOCALS_SIZE    544
#define X86_LEVEL_LOCALS_SIZE   69696

// x64 (64-bit) sizes - these will be calculated at runtime
// since they depend on the actual struct layouts

//=============================================================================
// Basic types
//=============================================================================

typedef unsigned char byte;
typedef float vec3_t[3];
typedef unsigned int uint;

typedef enum { qfalse, qtrue } qboolean;

typedef struct {
    union {
        struct { byte r, g, b, a; };
        uint c;
        byte c_array[4];
    };
} paletteRGBA_t;

typedef struct {
    byte r, g, b;
} paletteRGB_t;

typedef struct {
    int nodeinfo;
    paletteRGBA_t color;
    byte skin;
    byte _pad[3];
} fmnodeinfo_t;

typedef struct {
    short shrine_type;
    short pers_count;
    byte Amount[128];
} inventory_t;

//=============================================================================
// x86 (32-bit) struct definitions - using uint32_t for pointers
//=============================================================================

#pragma pack(push, 4)

// EffectsBuffer_t x86 layout
typedef struct {
    uint32_t buf;           // byte*
    int bufSize;
    int freeBlock;
    int numEffects;
} EffectsBuffer_x86_t;

// SinglyLinkedList_t x86 layout
typedef struct {
    uint32_t rearSentinel;
    uint32_t front;
    uint32_t current;
} SinglyLinkedList_x86_t;

// MsgQueue_t x86 layout
typedef struct {
    SinglyLinkedList_x86_t msgs;
} MsgQueue_x86_t;

// link_t x86 layout
typedef struct {
    uint32_t prev;
    uint32_t next;
} link_x86_t;

// moveinfo_t x86 layout
typedef struct {
    vec3_t start_origin;
    vec3_t start_angles;
    vec3_t end_origin;
    vec3_t end_angles;
    int sound_start;
    int sound_middle;
    int sound_end;
    float accel;
    float speed;
    float decel;
    float distance;
    float wait;
    int state;
    vec3_t dir;
    float current_speed;
    float move_speed;
    float next_speed;
    float remaining_distance;
    float decel_distance;
    uint32_t endfunc;
} moveinfo_x86_t;

// monsterinfo_t x86 layout
typedef struct {
    uint32_t otherenemyname;
    uint32_t currentmove;
    int aiflags;
    int aistate;
    int currframeindex;
    int nextframeindex;
    float thinkinc;
    float scale;
    uint32_t idle;
    uint32_t search;
    uint32_t dodge;
    uint32_t attack;
    uint32_t sight;
    uint32_t dismember;
    uint32_t alert;
    uint32_t checkattack;
    float pausetime;
    float attack_finished;
    float flee_finished;
    float chase_finished;
    vec3_t saved_goal;
    float search_time;
    float misc_debounce_time;
    vec3_t last_sighting;
    int attack_state;
    int lefty;
    float idle_time;
    int linkcount;
    int searchType;
    vec3_t nav_goal;
    float jump_time;
    int morcalavin_battle_phase;
    int ogleflags;
    int supporters;
    float sound_finished;
    float sound_start;
    int sound_pending;
    int c_dist;
    int c_repeat;
    uint32_t c_callback;
    int c_anim_flag;
    qboolean c_mode;
    uint32_t c_ent;
    qboolean awake;
    qboolean roared;
    float last_successful_enemy_tracking_time;
    float coop_check_debounce_time;
} monsterinfo_x86_t;

// alertent_t x86 layout
typedef struct {
    uint32_t next_alert;
    uint32_t prev_alert;
    uint32_t enemy;
    vec3_t origin;
    qboolean inuse;
    int alert_svflags;
    float lifetime;
} alertent_x86_t;

// buoy_t x86 layout
typedef struct {
    int nextbuoy[MAX_BUOY_BRANCHES];
    int modflags;
    int opflags;
    vec3_t origin;
    int id;
    uint32_t pathtarget;
    float wait;
    float delay;
    float temp_dist;
    float temp_e_dist;
    float jump_fspeed;
    float jump_yaw;
    float jump_uspeed;
    int jump_target_id;
    uint32_t target;
    uint32_t targetname;
    uint32_t jump_target;
} buoy_x86_t;

// entity_state_t x86 layout
typedef struct {
    short number;
    short frame;
    vec3_t origin;
    vec3_t angles;
    vec3_t old_origin;
    byte modelindex;
    byte _pad1;
    short clientnum;
    char skinname[MAX_QPATH];
    int skinnum;
    float scale;
    int effects;
    int renderfx;
    paletteRGBA_t color;
    paletteRGB_t absLight;
    byte _pad2;
    short solid;
    short _pad3;
    vec3_t mins;
    vec3_t maxs;
    byte sound;
    byte sound_data;
    short _pad4;
    vec3_t bmodel_origin;
    EffectsBuffer_x86_t clientEffects;
    fmnodeinfo_t fmnodeinfo[MAX_FM_MESH_NODES];
    short skeletalType;
    short rootJoint;
    short swapFrame;
    byte usageCount;
    byte _pad5;
} entity_state_x86_t;

// game_locals_t x86 layout
typedef struct {
    uint32_t clients;
    char spawnpoint[512];
    int maxclients;
    int maxentities;
    int num_clients;
    int serverflags;
    int num_items;
    qboolean autosaved;
    qboolean entitiesSpawned;
} game_locals_x86_t;

// level_locals_t x86 layout
typedef struct {
    int framenum;
    float time;
    char level_name[MAX_QPATH];
    char mapname[MAX_QPATH];
    char nextmap[MAX_QPATH];
    float intermissiontime;
    uint32_t changemap;
    qboolean exitintermission;
    vec3_t intermission_origin;
    vec3_t intermission_angle;
    uint32_t sight_client;
    uint32_t sight_entity;
    int sight_entity_framenum;
    float far_clip_dist_f;
    float fog;
    float fog_density;
    uint32_t current_entity;
    int body_que;
    buoy_x86_t buoy_list[MAX_MAP_BUOYS];
    int active_buoys;
    int fucked_buoys;
    int fixed_buoys;
    int player_buoy[MAX_CLIENTS];
    int player_last_buoy[MAX_CLIENTS];
    int offensive_weapons;
    int defensive_weapons;
    alertent_x86_t alertents[MAX_ALERT_ENTS];
    int num_alert_ents;
    uint32_t alert_entity;
    uint32_t last_alert;
    qboolean fighting_beast;
} level_locals_x86_t;

#pragma pack(pop)

//=============================================================================
// x64 (64-bit) struct definitions - using uint64_t for pointers
//=============================================================================

#pragma pack(push, 8)

// EffectsBuffer_t x64 layout
typedef struct {
    uint64_t buf;
    int bufSize;
    int freeBlock;
    int numEffects;
    int _pad;
} EffectsBuffer_x64_t;

// SinglyLinkedList_t x64 layout
typedef struct {
    uint64_t rearSentinel;
    uint64_t front;
    uint64_t current;
} SinglyLinkedList_x64_t;

// MsgQueue_t x64 layout
typedef struct {
    SinglyLinkedList_x64_t msgs;
} MsgQueue_x64_t;

// link_t x64 layout
typedef struct {
    uint64_t prev;
    uint64_t next;
} link_x64_t;

// moveinfo_t x64 layout
typedef struct {
    vec3_t start_origin;
    vec3_t start_angles;
    vec3_t end_origin;
    vec3_t end_angles;
    int sound_start;
    int sound_middle;
    int sound_end;
    float accel;
    float speed;
    float decel;
    float distance;
    float wait;
    int state;
    int _pad1;
    vec3_t dir;
    float current_speed;
    float move_speed;
    float next_speed;
    float remaining_distance;
    float decel_distance;
    int _pad2;
    uint64_t endfunc;
} moveinfo_x64_t;

// monsterinfo_t x64 layout
typedef struct {
    uint64_t otherenemyname;
    uint64_t currentmove;
    int aiflags;
    int aistate;
    int currframeindex;
    int nextframeindex;
    float thinkinc;
    float scale;
    uint64_t idle;
    uint64_t search;
    uint64_t dodge;
    uint64_t attack;
    uint64_t sight;
    uint64_t dismember;
    uint64_t alert;
    uint64_t checkattack;
    float pausetime;
    float attack_finished;
    float flee_finished;
    float chase_finished;
    vec3_t saved_goal;
    float search_time;
    float misc_debounce_time;
    vec3_t last_sighting;
    int attack_state;
    int lefty;
    float idle_time;
    int linkcount;
    int searchType;
    vec3_t nav_goal;
    float jump_time;
    int morcalavin_battle_phase;
    int ogleflags;
    int supporters;
    float sound_finished;
    float sound_start;
    int sound_pending;
    int c_dist;
    int c_repeat;
    int _pad1;
    uint64_t c_callback;
    int c_anim_flag;
    qboolean c_mode;
    uint64_t c_ent;
    qboolean awake;
    qboolean roared;
    float last_successful_enemy_tracking_time;
    float coop_check_debounce_time;
} monsterinfo_x64_t;

// alertent_t x64 layout
typedef struct {
    uint64_t next_alert;
    uint64_t prev_alert;
    uint64_t enemy;
    vec3_t origin;
    qboolean inuse;
    int alert_svflags;
    float lifetime;
} alertent_x64_t;

// buoy_t x64 layout
typedef struct {
    int nextbuoy[MAX_BUOY_BRANCHES];  // Indices, NOT pointers - same in x86 and x64
    int modflags;
    int opflags;
    vec3_t origin;
    int id;
    uint64_t pathtarget;  // char* - this IS a pointer, needs 8 bytes
    float wait;
    float delay;
    float temp_dist;
    float temp_e_dist;
    float jump_fspeed;
    float jump_yaw;
    float jump_uspeed;
    int jump_target_id;
    uint64_t target;
    uint64_t targetname;
    uint64_t jump_target;
} buoy_x64_t;

// entity_state_t x64 layout
typedef struct {
    short number;
    short frame;
    vec3_t origin;
    vec3_t angles;
    vec3_t old_origin;
    byte modelindex;
    byte _pad1;
    short clientnum;
    char skinname[MAX_QPATH];
    int skinnum;
    float scale;
    int effects;
    int renderfx;
    paletteRGBA_t color;
    paletteRGB_t absLight;
    byte _pad2;
    short solid;
    short _pad3;
    vec3_t mins;
    vec3_t maxs;
    byte sound;
    byte sound_data;
    short _pad4;
    vec3_t bmodel_origin;
    EffectsBuffer_x64_t clientEffects;
    fmnodeinfo_t fmnodeinfo[MAX_FM_MESH_NODES];
    short skeletalType;
    short rootJoint;
    short swapFrame;
    byte usageCount;
    byte _pad5;
} entity_state_x64_t;

// game_locals_t x64 layout
typedef struct {
    uint64_t clients;
    char spawnpoint[512];
    int maxclients;
    int maxentities;
    int num_clients;
    int serverflags;
    int num_items;
    qboolean autosaved;
    qboolean entitiesSpawned;
} game_locals_x64_t;

// level_locals_t x64 layout
typedef struct {
    int framenum;
    float time;
    char level_name[MAX_QPATH];
    char mapname[MAX_QPATH];
    char nextmap[MAX_QPATH];
    float intermissiontime;
    uint64_t changemap;
    qboolean exitintermission;
    int _pad1;
    vec3_t intermission_origin;
    vec3_t intermission_angle;
    uint64_t sight_client;
    uint64_t sight_entity;
    int sight_entity_framenum;
    float far_clip_dist_f;
    float fog;
    float fog_density;
    uint64_t current_entity;
    int body_que;
    int _pad2;
    buoy_x64_t buoy_list[MAX_MAP_BUOYS];
    int active_buoys;
    int fucked_buoys;
    int fixed_buoys;
    int player_buoy[MAX_CLIENTS];
    int player_last_buoy[MAX_CLIENTS];
    int offensive_weapons;
    int defensive_weapons;
    alertent_x64_t alertents[MAX_ALERT_ENTS];
    int num_alert_ents;
    int _pad3;
    uint64_t alert_entity;
    uint64_t last_alert;
    qboolean fighting_beast;
} level_locals_x64_t;

#pragma pack(pop)

//=============================================================================
// Function declarations
//=============================================================================

// Convert game save file (.sav)
bool ConvertGameSave(const char* inputPath, const char* outputPath);

// Convert level save file (.sv2)
bool ConvertLevelSave(const char* inputPath, const char* outputPath);

// Detect if a save file is 32-bit format
bool IsX86SaveFile(const char* path, int* outGameSize, int* outClientSize);

// Print struct sizes for debugging
void PrintStructSizes(void);
