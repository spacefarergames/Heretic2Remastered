//
// g_save_x86.c
//
// x86 (32-bit) save file compatibility for 64-bit builds.
// Contains conversion functions for reading old 32-bit saves.
//
// Copyright 2024
//

#include "g_save_x86.h"
#include "g_save.h"
#include "g_local.h"
#include "p_dll.h"
#include "sc_Main.h"
#include "Message.h"
#include "Utilities.h"

// Only compile this file for x64 builds
#ifdef _WIN64

//=============================================================================
// Size detection functions
//=============================================================================

qboolean IsX86Save_GameLocals(int stored_size)
{
    // x86 game_locals_t is ~544 bytes, x64 is ~552 bytes (one pointer difference)
    // Use exact known size for reliable detection
    return (stored_size == X86_GAME_LOCALS_SIZE);
}

qboolean IsX86Save_GClient(int stored_size)
{
    // x86 gclient_t is ~4380 bytes
    return (stored_size == X86_GCLIENT_SIZE);
}

qboolean IsX86Save_Edict(int stored_size)
{
    // Known x86 edict_t size is 1680
    return (stored_size == X86_EDICT_SIZE);
}

qboolean IsX86Save_LevelLocals(int stored_size)
{
    // x86 level_locals_t size varies with MAX_MAP_BUOYS and MAX_ALERT_ENTS
    // Use "smaller than x64" as heuristic since exact size is hard to predict
    return (stored_size < (int)sizeof(level_locals_t) && stored_size > 50000);
}

//=============================================================================
// Helper: Copy 4-byte x86 pointer field to 8-byte x64 pointer field
//=============================================================================

static void CopyPtrField(const byte* src, byte* dst)
{
    // Zero the 8-byte destination, then copy 4 bytes
    memset(dst, 0, 8);
    memcpy(dst, src, 4);
}

//=============================================================================
// Convert alertent_t from x86 to x64
//=============================================================================

static void ConvertAlertent_x86_to_x64(const alertent_x86_t* src, alertent_t* dst)
{
    memset(dst, 0, sizeof(*dst));
    
    // Pointer fields (will be cleared anyway, but copy the index data)
    CopyPtrField((const byte*)&src->next_alert, (byte*)&dst->next_alert);
    CopyPtrField((const byte*)&src->prev_alert, (byte*)&dst->prev_alert);
    CopyPtrField((const byte*)&src->enemy, (byte*)&dst->enemy);
    
    // Scalar fields
    VectorCopy(src->origin, dst->origin);
    dst->inuse = src->inuse;
    dst->alert_svflags = src->alert_svflags;
    dst->lifetime = src->lifetime;
}

//=============================================================================
// Convert buoy_t from x86 to x64
//=============================================================================

static void ConvertBuoy_x86_to_x64(const buoy_x86_t* src, buoy_t* dst)
{
    memset(dst, 0, sizeof(*dst));
    
    // Scalar fields
    for (int i = 0; i < MAX_BUOY_BRANCHES; i++)
        dst->nextbuoy[i] = src->nextbuoy[i];
    dst->modflags = src->modflags;
    dst->opflags = src->opflags;
    VectorCopy(src->origin, dst->origin);
    dst->id = src->id;
    dst->wait = src->wait;
    dst->delay = src->delay;
    dst->temp_dist = src->temp_dist;
    dst->temp_e_dist = src->temp_e_dist;
    dst->jump_fspeed = src->jump_fspeed;
    dst->jump_yaw = src->jump_yaw;
    dst->jump_uspeed = src->jump_uspeed;
    dst->jump_target_id = src->jump_target_id;
    
    // String pointer fields - store the length/index that was written
    CopyPtrField((const byte*)&src->pathtarget, (byte*)&dst->pathtarget);
    CopyPtrField((const byte*)&src->target, (byte*)&dst->target);
    CopyPtrField((const byte*)&src->targetname, (byte*)&dst->targetname);
    CopyPtrField((const byte*)&src->jump_target, (byte*)&dst->jump_target);
}

//=============================================================================
// Convert level_locals_t from x86 to x64
//=============================================================================

static void ConvertLevelLocals_x86_to_x64(const level_locals_x86_t* src, level_locals_t* dst)
{
    memset(dst, 0, sizeof(*dst));
    
    // Scalar fields before pointers
    dst->framenum = src->framenum;
    dst->time = src->time;
    memcpy(dst->level_name, src->level_name, sizeof(dst->level_name));
    memcpy(dst->mapname, src->mapname, sizeof(dst->mapname));
    memcpy(dst->nextmap, src->nextmap, sizeof(dst->nextmap));
    dst->intermissiontime = src->intermissiontime;
    
    // Pointer field: changemap (string length stored as int)
    CopyPtrField((const byte*)&src->changemap, (byte*)&dst->changemap);
    
    dst->exitintermission = src->exitintermission;
    VectorCopy(src->intermission_origin, dst->intermission_origin);
    VectorCopy(src->intermission_angle, dst->intermission_angle);
    
    // Pointer fields: sight_client, sight_entity (edict indices)
    CopyPtrField((const byte*)&src->sight_client, (byte*)&dst->sight_client);
    CopyPtrField((const byte*)&src->sight_entity, (byte*)&dst->sight_entity);
    
    dst->sight_entity_framenum = src->sight_entity_framenum;
    dst->far_clip_dist_f = src->far_clip_dist_f;
    dst->fog = src->fog;
    dst->fog_density = src->fog_density;
    
    // Pointer field: current_entity (will be cleared)
    CopyPtrField((const byte*)&src->current_entity, (byte*)&dst->current_entity);
    
    dst->body_que = src->body_que;
    
    // Convert buoy array
    for (int i = 0; i < MAX_MAP_BUOYS; i++)
        ConvertBuoy_x86_to_x64(&src->buoy_list[i], &dst->buoy_list[i]);
    
    dst->active_buoys = src->active_buoys;
    dst->fucked_buoys = src->fucked_buoys;
    dst->fixed_buoys = src->fixed_buoys;
    
    memcpy(dst->player_buoy, src->player_buoy, sizeof(dst->player_buoy));
    memcpy(dst->player_last_buoy, src->player_last_buoy, sizeof(dst->player_last_buoy));
    
    dst->offensive_weapons = src->offensive_weapons;
    dst->defensive_weapons = src->defensive_weapons;
    
    // Convert alertent array
    for (int i = 0; i < MAX_ALERT_ENTS; i++)
        ConvertAlertent_x86_to_x64(&src->alertents[i], &dst->alertents[i]);
    
    dst->num_alert_ents = src->num_alert_ents;
    
    // Pointer fields: alert_entity, last_alert (will be cleared)
    CopyPtrField((const byte*)&src->alert_entity, (byte*)&dst->alert_entity);
    CopyPtrField((const byte*)&src->last_alert, (byte*)&dst->last_alert);
    
    dst->fighting_beast = src->fighting_beast;
}

//=============================================================================
// Convert game_locals_t from x86 to x64
//=============================================================================

static void ConvertGameLocals_x86_to_x64(const game_locals_x86_t* src, game_locals_t* dst)
{
    memset(dst, 0, sizeof(*dst));
    
    // Pointer field: clients (index stored as int)
    // Note: This pointer is re-allocated after loading, so we just need to preserve the conversion
    CopyPtrField((const byte*)&src->clients, (byte*)&dst->clients);
    
    memcpy(dst->spawnpoint, src->spawnpoint, sizeof(dst->spawnpoint));
    dst->maxclients = src->maxclients;
    dst->maxentities = src->maxentities;
    dst->num_clients = src->num_clients;
    dst->serverflags = src->serverflags;
    dst->num_items = src->num_items;
    dst->autosaved = src->autosaved;
    dst->entitiesSpawned = src->entitiesSpawned;
}

//=============================================================================
// Read x86 level_locals_t from file and convert to x64
//=============================================================================

qboolean ReadLevelLocals_x86(FILE* f, int stored_size)
{
    // Allocate buffer for x86 struct
    byte* x86_buf = (byte*)malloc(stored_size);
    if (x86_buf == NULL)
    {
        gi.error("ReadLevelLocals_x86: Failed to allocate %d bytes", stored_size);
        return false;
    }
    
    // Read x86 data
    if (fread(x86_buf, stored_size, 1, f) != 1)
    {
        free(x86_buf);
        gi.error("ReadLevelLocals_x86: Failed to read level locals");
        return false;
    }
    
    // Check if stored size matches expected x86 size
    if (stored_size == sizeof(level_locals_x86_t))
    {
        // Direct conversion
        ConvertLevelLocals_x86_to_x64((const level_locals_x86_t*)x86_buf, &level);
    }
    else
    {
        // Size doesn't match our shadow struct - may be from a different x86 build
        free(x86_buf);
        gi.error("ReadLevelLocals_x86: Unexpected x86 level_locals size %d (expected %zu)", 
                 stored_size, sizeof(level_locals_x86_t));
        return false;
    }
    
    free(x86_buf);
    
    gi.dprintf("ReadLevelLocals_x86: Converted x86 level_locals_t (%d bytes) to x64 (%zu bytes)\n",
               stored_size, sizeof(level_locals_t));
    
    return true;
}

//=============================================================================
// Read x86 game data from file and convert to x64
// This handles game_locals_t and all gclient_t structures
//=============================================================================

qboolean ReadGame_x86(FILE* f, int stored_game_size)
{
    // For now, we don't support x86 game saves - they're too complex due to
    // gclient_t containing playerinfo_t which has ~40 function pointers.
    // The function pointer positions in playerinfo_t differ between x86/x64,
    // making reliable conversion extremely difficult.

    // Use Com_Printf if available, otherwise fall through to gi.error
    if (gi.dprintf != NULL)
    {
        gi.dprintf("***ERROR*** ReadGame_x86: x86 (32-bit) saves not supported in 64-bit build.\n");
        gi.dprintf("stored game_size=%d, expected x64 size=%zu\n", stored_game_size, sizeof(game_locals_t));
    }

    if (gi.error != NULL)
        gi.error("Cannot load 32-bit save in 64-bit build");

    return false;
}

//=============================================================================
// Read x86 level data from file
// This handles the entire level loading process for x86 saves.
//=============================================================================

qboolean ReadLevel_x86(FILE* f, int stored_edict_size)
{
    // For now, we don't support x86 level saves - they're too complex due to
    // edict_t containing many embedded structs with pointers.

    if (gi.dprintf != NULL)
    {
        gi.dprintf("***ERROR*** ReadLevel_x86: x86 (32-bit) saves not supported in 64-bit build.\n");
        gi.dprintf("stored edict_size=%d, expected x64 size=%zu\n", stored_edict_size, sizeof(edict_t));
    }

    if (gi.error != NULL)
        gi.error("Cannot load 32-bit save in 64-bit build");

    return false;
}

//=============================================================================
// Read x86 edict_t from file and convert to x64
// This is extremely complex due to the many embedded structs with pointers.
// For now, we don't support this conversion.
//=============================================================================

qboolean ReadEdict_x86(FILE* f, edict_t* ent)
{
    // edict_t is extremely complex with many embedded structs containing pointers.
    // For now, we report an error.

    if (gi.dprintf != NULL)
        gi.dprintf("***ERROR*** ReadEdict_x86: x86 (32-bit) saves not supported in 64-bit build.\n");

    if (gi.error != NULL)
        gi.error("Cannot load 32-bit save in 64-bit build");

    return false;
}

#else // !_WIN64

// Stub implementations for x86 builds (no conversion needed)

qboolean IsX86Save_GameLocals(int stored_size) { return false; }
qboolean IsX86Save_GClient(int stored_size) { return false; }
qboolean IsX86Save_Edict(int stored_size) { return false; }
qboolean IsX86Save_LevelLocals(int stored_size) { return false; }

qboolean ReadGame_x86(FILE* f, int stored_game_size) { return false; }
qboolean ReadLevel_x86(FILE* f, int stored_edict_size) { return false; }
qboolean ReadLevelLocals_x86(FILE* f, int stored_size) { return false; }
qboolean ReadEdict_x86(FILE* f, edict_t* ent) { return false; }

#endif // _WIN64
