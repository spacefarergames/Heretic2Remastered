//
// save_convert.c
//
// Heretic 2 Save File Converter - 32-bit to 64-bit
//

#include "save_convert.h"

//=============================================================================
// Helper functions
//=============================================================================

static void VectorCopy(const vec3_t src, vec3_t dst)
{
    dst[0] = src[0];
    dst[1] = src[1];
    dst[2] = src[2];
}

// Copy x86 pointer value to x64 pointer field (preserving index values)
static void CopyPtr32to64(uint32_t src, uint64_t* dst)
{
    // Pointers in save files are stored as indices, not actual pointers
    // So we just zero-extend the 32-bit value to 64-bit
    *dst = (uint64_t)src;
}

//=============================================================================
// Struct conversion functions
//=============================================================================

static void ConvertEffectsBuffer(const EffectsBuffer_x86_t* src, EffectsBuffer_x64_t* dst)
{
    memset(dst, 0, sizeof(*dst));
    CopyPtr32to64(src->buf, &dst->buf);
    dst->bufSize = src->bufSize;
    dst->freeBlock = src->freeBlock;
    dst->numEffects = src->numEffects;
}

static void ConvertMoveinfo(const moveinfo_x86_t* src, moveinfo_x64_t* dst)
{
    memset(dst, 0, sizeof(*dst));
    VectorCopy(src->start_origin, dst->start_origin);
    VectorCopy(src->start_angles, dst->start_angles);
    VectorCopy(src->end_origin, dst->end_origin);
    VectorCopy(src->end_angles, dst->end_angles);
    dst->sound_start = src->sound_start;
    dst->sound_middle = src->sound_middle;
    dst->sound_end = src->sound_end;
    dst->accel = src->accel;
    dst->speed = src->speed;
    dst->decel = src->decel;
    dst->distance = src->distance;
    dst->wait = src->wait;
    dst->state = src->state;
    VectorCopy(src->dir, dst->dir);
    dst->current_speed = src->current_speed;
    dst->move_speed = src->move_speed;
    dst->next_speed = src->next_speed;
    dst->remaining_distance = src->remaining_distance;
    dst->decel_distance = src->decel_distance;
    CopyPtr32to64(src->endfunc, &dst->endfunc);
}

static void ConvertMonsterinfo(const monsterinfo_x86_t* src, monsterinfo_x64_t* dst)
{
    memset(dst, 0, sizeof(*dst));
    CopyPtr32to64(src->otherenemyname, &dst->otherenemyname);
    CopyPtr32to64(src->currentmove, &dst->currentmove);
    dst->aiflags = src->aiflags;
    dst->aistate = src->aistate;
    dst->currframeindex = src->currframeindex;
    dst->nextframeindex = src->nextframeindex;
    dst->thinkinc = src->thinkinc;
    dst->scale = src->scale;
    CopyPtr32to64(src->idle, &dst->idle);
    CopyPtr32to64(src->search, &dst->search);
    CopyPtr32to64(src->dodge, &dst->dodge);
    CopyPtr32to64(src->attack, &dst->attack);
    CopyPtr32to64(src->sight, &dst->sight);
    CopyPtr32to64(src->dismember, &dst->dismember);
    CopyPtr32to64(src->alert, &dst->alert);
    CopyPtr32to64(src->checkattack, &dst->checkattack);
    dst->pausetime = src->pausetime;
    dst->attack_finished = src->attack_finished;
    dst->flee_finished = src->flee_finished;
    dst->chase_finished = src->chase_finished;
    VectorCopy(src->saved_goal, dst->saved_goal);
    dst->search_time = src->search_time;
    dst->misc_debounce_time = src->misc_debounce_time;
    VectorCopy(src->last_sighting, dst->last_sighting);
    dst->attack_state = src->attack_state;
    dst->lefty = src->lefty;
    dst->idle_time = src->idle_time;
    dst->linkcount = src->linkcount;
    dst->searchType = src->searchType;
    VectorCopy(src->nav_goal, dst->nav_goal);
    dst->jump_time = src->jump_time;
    dst->morcalavin_battle_phase = src->morcalavin_battle_phase;
    dst->ogleflags = src->ogleflags;
    dst->supporters = src->supporters;
    dst->sound_finished = src->sound_finished;
    dst->sound_start = src->sound_start;
    dst->sound_pending = src->sound_pending;
    dst->c_dist = src->c_dist;
    dst->c_repeat = src->c_repeat;
    CopyPtr32to64(src->c_callback, &dst->c_callback);
    dst->c_anim_flag = src->c_anim_flag;
    dst->c_mode = src->c_mode;
    CopyPtr32to64(src->c_ent, &dst->c_ent);
    dst->awake = src->awake;
    dst->roared = src->roared;
    dst->last_successful_enemy_tracking_time = src->last_successful_enemy_tracking_time;
    dst->coop_check_debounce_time = src->coop_check_debounce_time;
}

static void ConvertAlertent(const alertent_x86_t* src, alertent_x64_t* dst)
{
    memset(dst, 0, sizeof(*dst));
    CopyPtr32to64(src->next_alert, &dst->next_alert);
    CopyPtr32to64(src->prev_alert, &dst->prev_alert);
    CopyPtr32to64(src->enemy, &dst->enemy);
    VectorCopy(src->origin, dst->origin);
    dst->inuse = src->inuse;
    dst->alert_svflags = src->alert_svflags;
    dst->lifetime = src->lifetime;
}

static void ConvertBuoy(const buoy_x86_t* src, buoy_x64_t* dst)
{
    memset(dst, 0, sizeof(*dst));
    // Note: In x86, nextbuoy[] are ints (indices), in x64 they're pointers
    // But in save files, they're stored as indices, so we zero-extend
    for (int i = 0; i < MAX_BUOY_BRANCHES; i++)
        dst->nextbuoy[i] = (uint64_t)src->nextbuoy[i];
    dst->modflags = src->modflags;
    dst->opflags = src->opflags;
    VectorCopy(src->origin, dst->origin);
    dst->id = src->id;
    CopyPtr32to64(src->pathtarget, &dst->pathtarget);
    dst->wait = src->wait;
    dst->delay = src->delay;
    dst->temp_dist = src->temp_dist;
    dst->temp_e_dist = src->temp_e_dist;
    dst->jump_fspeed = src->jump_fspeed;
    dst->jump_yaw = src->jump_yaw;
    dst->jump_uspeed = src->jump_uspeed;
    dst->jump_target_id = src->jump_target_id;
    CopyPtr32to64(src->target, &dst->target);
    CopyPtr32to64(src->targetname, &dst->targetname);
    CopyPtr32to64(src->jump_target, &dst->jump_target);
}

static void ConvertGameLocals(const game_locals_x86_t* src, game_locals_x64_t* dst)
{
    memset(dst, 0, sizeof(*dst));
    CopyPtr32to64(src->clients, &dst->clients);
    memcpy(dst->spawnpoint, src->spawnpoint, sizeof(dst->spawnpoint));
    dst->maxclients = src->maxclients;
    dst->maxentities = src->maxentities;
    dst->num_clients = src->num_clients;
    dst->serverflags = src->serverflags;
    dst->num_items = src->num_items;
    dst->autosaved = src->autosaved;
    dst->entitiesSpawned = src->entitiesSpawned;
}

static void ConvertLevelLocals(const level_locals_x86_t* src, level_locals_x64_t* dst)
{
    memset(dst, 0, sizeof(*dst));
    dst->framenum = src->framenum;
    dst->time = src->time;
    memcpy(dst->level_name, src->level_name, sizeof(dst->level_name));
    memcpy(dst->mapname, src->mapname, sizeof(dst->mapname));
    memcpy(dst->nextmap, src->nextmap, sizeof(dst->nextmap));
    dst->intermissiontime = src->intermissiontime;
    CopyPtr32to64(src->changemap, &dst->changemap);
    dst->exitintermission = src->exitintermission;
    VectorCopy(src->intermission_origin, dst->intermission_origin);
    VectorCopy(src->intermission_angle, dst->intermission_angle);
    CopyPtr32to64(src->sight_client, &dst->sight_client);
    CopyPtr32to64(src->sight_entity, &dst->sight_entity);
    dst->sight_entity_framenum = src->sight_entity_framenum;
    dst->far_clip_dist_f = src->far_clip_dist_f;
    dst->fog = src->fog;
    dst->fog_density = src->fog_density;
    CopyPtr32to64(src->current_entity, &dst->current_entity);
    dst->body_que = src->body_que;
    
    // Convert buoy list
    for (int i = 0; i < MAX_MAP_BUOYS; i++)
        ConvertBuoy(&src->buoy_list[i], &dst->buoy_list[i]);
    
    dst->active_buoys = src->active_buoys;
    dst->fucked_buoys = src->fucked_buoys;
    dst->fixed_buoys = src->fixed_buoys;
    memcpy(dst->player_buoy, src->player_buoy, sizeof(dst->player_buoy));
    memcpy(dst->player_last_buoy, src->player_last_buoy, sizeof(dst->player_last_buoy));
    dst->offensive_weapons = src->offensive_weapons;
    dst->defensive_weapons = src->defensive_weapons;
    
    // Convert alertents
    for (int i = 0; i < MAX_ALERT_ENTS; i++)
        ConvertAlertent(&src->alertents[i], &dst->alertents[i]);
    
    dst->num_alert_ents = src->num_alert_ents;
    CopyPtr32to64(src->alert_entity, &dst->alert_entity);
    CopyPtr32to64(src->last_alert, &dst->last_alert);
    dst->fighting_beast = src->fighting_beast;
}

//=============================================================================
// Public functions
//=============================================================================

void PrintStructSizes(void)
{
    printf("=== x86 (32-bit) Struct Sizes ===\n");
    printf("  game_locals_x86_t:  %zu bytes\n", sizeof(game_locals_x86_t));
    printf("  level_locals_x86_t: %zu bytes\n", sizeof(level_locals_x86_t));
    printf("  buoy_x86_t:         %zu bytes\n", sizeof(buoy_x86_t));
    printf("  alertent_x86_t:     %zu bytes\n", sizeof(alertent_x86_t));
    printf("  monsterinfo_x86_t:  %zu bytes\n", sizeof(monsterinfo_x86_t));
    printf("  moveinfo_x86_t:     %zu bytes\n", sizeof(moveinfo_x86_t));
    printf("\n");
    printf("=== x64 (64-bit) Struct Sizes ===\n");
    printf("  game_locals_x64_t:  %zu bytes\n", sizeof(game_locals_x64_t));
    printf("  level_locals_x64_t: %zu bytes\n", sizeof(level_locals_x64_t));
    printf("  buoy_x64_t:         %zu bytes\n", sizeof(buoy_x64_t));
    printf("  alertent_x64_t:     %zu bytes\n", sizeof(alertent_x64_t));
    printf("  monsterinfo_x64_t:  %zu bytes\n", sizeof(monsterinfo_x64_t));
    printf("  moveinfo_x64_t:     %zu bytes\n", sizeof(moveinfo_x64_t));
    printf("\n");
}

bool IsX86SaveFile(const char* path, int* outGameSize, int* outClientSize)
{
    FILE* f = NULL;
    if (fopen_s(&f, path, "rb") != 0 || f == NULL)
        return false;
    
    // Read save version
    char save_ver[16];
    if (fread(save_ver, sizeof(save_ver), 1, f) != 1)
    {
        fclose(f);
        return false;
    }
    save_ver[15] = 0;
    
    if (strcmp(save_ver, SAVE_VERSION) != 0)
    {
        fclose(f);
        printf("Warning: Unknown save version '%s'\n", save_ver);
        return false;
    }
    
    // Read game_locals size
    int game_size;
    if (fread(&game_size, sizeof(game_size), 1, f) != 1)
    {
        fclose(f);
        return false;
    }
    
    if (outGameSize)
        *outGameSize = game_size;
    
    fclose(f);
    
    // Check if it's an x86 save (smaller than x64 size)
    return (game_size < (int)sizeof(game_locals_x64_t) && game_size > 0);
}

bool ConvertGameSave(const char* inputPath, const char* outputPath)
{
    printf("Converting game save: %s\n", inputPath);
    
    FILE* fin = NULL;
    if (fopen_s(&fin, inputPath, "rb") != 0 || fin == NULL)
    {
        printf("Error: Cannot open input file: %s\n", inputPath);
        return false;
    }
    
    // Get file size
    fseek(fin, 0, SEEK_END);
    long fileSize = ftell(fin);
    fseek(fin, 0, SEEK_SET);
    
    printf("  Input file size: %ld bytes\n", fileSize);
    
    // Read save version
    char save_ver[16];
    if (fread(save_ver, sizeof(save_ver), 1, fin) != 1)
    {
        printf("Error: Cannot read save version\n");
        fclose(fin);
        return false;
    }
    save_ver[15] = 0;
    printf("  Save version: %s\n", save_ver);
    
    // Read x86 game_locals size
    int x86_game_size;
    if (fread(&x86_game_size, sizeof(x86_game_size), 1, fin) != 1)
    {
        printf("Error: Cannot read game_locals size\n");
        fclose(fin);
        return false;
    }
    printf("  x86 game_locals size: %d bytes\n", x86_game_size);
    printf("  Expected x64 size:    %zu bytes\n", sizeof(game_locals_x64_t));
    
    // Read x86 game_locals
    game_locals_x86_t game_x86;
    if (fread(&game_x86, x86_game_size, 1, fin) != 1)
    {
        printf("Error: Cannot read game_locals data\n");
        fclose(fin);
        return false;
    }
    printf("  maxclients: %d, maxentities: %d\n", game_x86.maxclients, game_x86.maxentities);
    
    // Read x86 client size
    int x86_client_size;
    if (fread(&x86_client_size, sizeof(x86_client_size), 1, fin) != 1)
    {
        printf("Error: Cannot read client size\n");
        fclose(fin);
        return false;
    }
    printf("  x86 gclient size: %d bytes\n", x86_client_size);
    
    // For now, we'll skip client conversion - it's extremely complex
    // Just read and write the raw client data (the game will reinitialize it)
    long clientDataStart = ftell(fin);
    long clientDataSize = (long)x86_client_size * game_x86.maxclients;
    
    byte* clientData = (byte*)malloc(clientDataSize);
    if (clientData == NULL)
    {
        printf("Error: Cannot allocate memory for client data\n");
        fclose(fin);
        return false;
    }
    
    if (fread(clientData, clientDataSize, 1, fin) != 1)
    {
        printf("Error: Cannot read client data\n");
        free(clientData);
        fclose(fin);
        return false;
    }
    
    // Read remaining data (scripts, etc.)
    long remainingStart = ftell(fin);
    long remainingSize = fileSize - remainingStart;
    
    byte* remainingData = NULL;
    if (remainingSize > 0)
    {
        remainingData = (byte*)malloc(remainingSize);
        if (remainingData == NULL)
        {
            printf("Error: Cannot allocate memory for remaining data\n");
            free(clientData);
            fclose(fin);
            return false;
        }
        
        if (fread(remainingData, remainingSize, 1, fin) != 1)
        {
            printf("Warning: Could not read all remaining data\n");
        }
    }
    
    fclose(fin);
    
    // Convert game_locals
    game_locals_x64_t game_x64;
    ConvertGameLocals(&game_x86, &game_x64);
    
    // Write output file
    FILE* fout = NULL;
    if (fopen_s(&fout, outputPath, "wb") != 0 || fout == NULL)
    {
        printf("Error: Cannot create output file: %s\n", outputPath);
        free(clientData);
        if (remainingData) free(remainingData);
        return false;
    }
    
    // Write save version
    fwrite(save_ver, sizeof(save_ver), 1, fout);
    
    // Write x64 game_locals size
    int x64_game_size = (int)sizeof(game_locals_x64_t);
    fwrite(&x64_game_size, sizeof(x64_game_size), 1, fout);
    
    // Write x64 game_locals
    fwrite(&game_x64, sizeof(game_x64), 1, fout);
    
    // Write x64 client size (we're not actually converting clients, so this won't work properly)
    // The game will need to reinitialize clients on load
    printf("\n  WARNING: Client data conversion is not fully implemented.\n");
    printf("  The converted save may not work correctly.\n");
    printf("  You may need to start a new game after loading.\n\n");
    
    // Write a placeholder client size that signals "needs reinitialization"
    // For now, just write the original client data
    fwrite(&x86_client_size, sizeof(x86_client_size), 1, fout);
    fwrite(clientData, clientDataSize, 1, fout);
    
    // Write remaining data
    if (remainingData && remainingSize > 0)
        fwrite(remainingData, remainingSize, 1, fout);
    
    fclose(fout);
    
    free(clientData);
    if (remainingData) free(remainingData);
    
    printf("  Output file created: %s\n", outputPath);
    printf("  Conversion complete (partial - game_locals only)\n");
    
    return true;
}

bool ConvertLevelSave(const char* inputPath, const char* outputPath)
{
    printf("Converting level save: %s\n", inputPath);
    printf("  Level save conversion is not yet implemented.\n");
    printf("  Level saves (.sv2) are more complex and require\n");
    printf("  conversion of edict_t structures.\n");
    return false;
}
