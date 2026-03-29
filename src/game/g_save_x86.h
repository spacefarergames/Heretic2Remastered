//
// g_save_x86.h
//
// x86 (32-bit) save file compatibility for 64-bit builds.
// Contains x86 shadow struct definitions used for reading old 32-bit saves.
//
// Copyright 2024
//

#pragma once

#include <stdint.h>
#include <stdio.h>
#include "q_Typedef.h"

// Constants needed for x86 shadow structs (must match original definitions)
#ifndef MAX_QPATH
#define MAX_QPATH           64
#endif
#ifndef MAX_INFO_STRING
#define MAX_INFO_STRING     512
#endif
#ifndef MAX_FM_MESH_NODES
#define MAX_FM_MESH_NODES   16
#endif
#ifndef MAX_BUOY_BRANCHES
#define MAX_BUOY_BRANCHES   3
#endif
#ifndef MAX_MAP_BUOYS
#define MAX_MAP_BUOYS       256
#endif
#ifndef MAX_ALERT_ENTS
#define MAX_ALERT_ENTS      1024
#endif
#ifndef MAX_CLIENTS
#define MAX_CLIENTS         8
#endif

// Types needed for x86 shadow structs
typedef struct {
    byte r, g, b;
} paletteRGB_x86_t;

typedef struct {
    int nodeinfo;
    paletteRGBA_t color;
    byte skin;
    byte _pad[3];
} fmnodeinfo_x86_t;

typedef struct {
    short shrine_type;
    short pers_count;
    byte Amount[128];
} inventory_x86_t;

// Use uint32_t for all pointer types to match x86 pointer size (4 bytes).
// This gives us the correct x86 struct layout on x64 builds.

#pragma pack(push, 4) // Ensure x86-compatible packing

//=============================================================================
// Known x86 struct sizes (used for detection and verification)
//=============================================================================

#define X86_EDICT_SIZE          1680
#define X86_GCLIENT_SIZE        4380  // Approximate, will verify
#define X86_GAME_LOCALS_SIZE    544
#define X86_LEVEL_LOCALS_SIZE   69928 // Approximate, depends on MAX_MAP_BUOYS and MAX_ALERT_ENTS

//=============================================================================
// x86 shadow structs for embedded types with pointers
//=============================================================================

// EffectsBuffer_t x86 layout (embeds in entity_state_t)
typedef struct {
    uint32_t buf;           // byte*
    int bufSize;
    int freeBlock;
    int numEffects;
} EffectsBuffer_x86_t;

// SinglyLinkedList_t x86 layout (embeds in MsgQueue_t)
typedef struct {
    uint32_t rearSentinel;  // SinglyLinkedListNode_s*
    uint32_t front;         // SinglyLinkedListNode_s*
    uint32_t current;       // SinglyLinkedListNode_s*
} SinglyLinkedList_x86_t;

// MsgQueue_t x86 layout (embeds in edict_t)
typedef struct {
    SinglyLinkedList_x86_t msgs;
} MsgQueue_x86_t;

// link_t x86 layout (embeds in edict_t)
typedef struct {
    uint32_t prev;          // link_s*
    uint32_t next;          // link_s*
} link_x86_t;

// moveinfo_t x86 layout (embeds in edict_t)
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
    uint32_t endfunc;       // void (*)(edict_t*)
} moveinfo_x86_t;

// monsterinfo_t x86 layout (embeds in edict_t)
typedef struct {
    uint32_t otherenemyname;    // char*
    uint32_t currentmove;       // animmove_t*
    int aiflags;
    int aistate;
    int currframeindex;
    int nextframeindex;
    float thinkinc;
    float scale;
    uint32_t idle;              // void (*)(edict_t*)
    uint32_t search;            // void (*)(edict_t*)
    uint32_t dodge;             // void (*)(edict_t*, edict_t*, float)
    uint32_t attack;            // int (*)(edict_t*)
    uint32_t sight;             // void (*)(edict_t*, edict_t*)
    uint32_t dismember;         // void (*)(edict_t*, int, HitLocation_t)
    uint32_t alert;             // qboolean (*)(edict_t*, alertent_t*, edict_t*)
    uint32_t checkattack;       // qboolean (*)(edict_t*)
    float pausetime;
    float attack_finished;
    float flee_finished;        // union
    float chase_finished;       // union
    vec3_t saved_goal;          // union
    float search_time;          // union
    float misc_debounce_time;
    vec3_t last_sighting;
    int attack_state;
    int lefty;                  // union
    float idle_time;
    int linkcount;
    int searchType;
    vec3_t nav_goal;
    float jump_time;            // union
    int morcalavin_battle_phase;
    int ogleflags;
    int supporters;
    float sound_finished;
    float sound_start;          // union
    int sound_pending;
    int c_dist;
    int c_repeat;
    uint32_t c_callback;        // void (*)(edict_t*)
    int c_anim_flag;
    qboolean c_mode;
    uint32_t c_ent;             // edict_t*
    qboolean awake;
    qboolean roared;
    float last_successful_enemy_tracking_time;
    float coop_check_debounce_time;
} monsterinfo_x86_t;

// alertent_t x86 layout (embeds in level_locals_t)
typedef struct {
    uint32_t next_alert;        // alertent_t*
    uint32_t prev_alert;        // alertent_t*
    uint32_t enemy;             // edict_t*
    vec3_t origin;
    qboolean inuse;
    int alert_svflags;
    float lifetime;
} alertent_x86_t;

// buoy_t x86 layout (embeds in level_locals_t)
typedef struct {
    int nextbuoy[MAX_BUOY_BRANCHES];
    int modflags;
    int opflags;
    vec3_t origin;
    int id;
    uint32_t pathtarget;        // char*
    float wait;
    float delay;
    float temp_dist;
    float temp_e_dist;
    float jump_fspeed;
    float jump_yaw;
    float jump_uspeed;
    int jump_target_id;
    uint32_t target;            // char*
    uint32_t targetname;        // char*
    uint32_t jump_target;       // char*
} buoy_x86_t;

// client_persistant_t x86 layout (embeds in playerinfo_t)
typedef struct {
    char userinfo[MAX_INFO_STRING];
    char netname[16];
    char sounddir[MAX_QPATH];
    int autoweapon;
    qboolean connected;
    int health;
    int max_health;
    short mission_num1;
    short mission_num2;
    int weaponready;
    byte armortype;
    byte bowtype;
    byte stafflevel;
    byte helltype;
    byte handfxtype;
    float armor_count;
    short skintype;
    uint altparts;
    inventory_x86_t inventory;
    inventory_x86_t old_inventory;
    int selected_item;
    int max_offmana;
    int max_defmana;
    int max_redarrow;
    int max_phoenarr;
    int max_hellstaff;
    uint32_t weapon;            // gitem_t*
    uint32_t lastweapon;        // gitem_t*
    uint32_t defence;           // gitem_t*
    uint32_t lastdefence;       // gitem_t*
    uint32_t newweapon;         // gitem_t*
    int score;
} client_persistant_x86_t;

//=============================================================================
// x86 shadow struct for entity_state_t (embeds in edict_t)
//=============================================================================

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
    paletteRGB_x86_t absLight;
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
    fmnodeinfo_x86_t fmnodeinfo[MAX_FM_MESH_NODES];
    short skeletalType;
    short rootJoint;
    short swapFrame;
    byte usageCount;
    byte _pad5;
} entity_state_x86_t;

//=============================================================================
// x86 shadow struct for game_locals_t
//=============================================================================

typedef struct {
    uint32_t clients;           // gclient_t*
    char spawnpoint[512];
    int maxclients;
    int maxentities;
    int num_clients;
    int serverflags;
    int num_items;
    qboolean autosaved;
    qboolean entitiesSpawned;
} game_locals_x86_t;

//=============================================================================
// x86 shadow struct for level_locals_t
//=============================================================================

typedef struct {
    int framenum;
    float time;
    char level_name[MAX_QPATH];
    char mapname[MAX_QPATH];
    char nextmap[MAX_QPATH];
    float intermissiontime;
    uint32_t changemap;         // char*
    qboolean exitintermission;
    vec3_t intermission_origin;
    vec3_t intermission_angle;
    uint32_t sight_client;      // edict_t*
    uint32_t sight_entity;      // edict_t*
    int sight_entity_framenum;
    float far_clip_dist_f;
    float fog;
    float fog_density;
    uint32_t current_entity;    // edict_t*
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
    uint32_t alert_entity;      // alertent_t*
    uint32_t last_alert;        // alertent_t*
    qboolean fighting_beast;
} level_locals_x86_t;

#pragma pack(pop)

//=============================================================================
// Conversion function declarations
//=============================================================================

// Check if a save file is from x86 build based on stored struct size
qboolean IsX86Save_GameLocals(int stored_size);
qboolean IsX86Save_GClient(int stored_size);
qboolean IsX86Save_Edict(int stored_size);
qboolean IsX86Save_LevelLocals(int stored_size);

// Read x86 struct from file and convert to x64
qboolean ReadGame_x86(FILE* f, int stored_game_size);
qboolean ReadLevel_x86(FILE* f, int stored_edict_size);
qboolean ReadLevelLocals_x86(FILE* f, int stored_size);
qboolean ReadEdict_x86(FILE* f, edict_t* ent);
