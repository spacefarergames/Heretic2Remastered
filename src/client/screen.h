//
// screen.h
//
// Copyright 1998 Raven Software
//

#pragma once

#include "q_shared.h" //mxd
#include "vid.h" //mxd

typedef struct GameMessageDisplayInfo_s //mxd
{
	char message[1024];
	int num_lines;
	paletteRGBA_t color;
	qboolean is_caption;
	float dispay_time;
	float fade_in_duration;   //mxd. Duration of fade-in effect in seconds.
	float fade_out_duration;  //mxd. Duration of fade-out effect in seconds.
	float total_display_time; //mxd. Total time message should be displayed (for fade calculations).
} GameMessageDisplayInfo_t;

extern GameMessageDisplayInfo_t display_msg; //mxd

extern qboolean scr_draw_loading_plaque; //mxd

extern float scr_con_current;

//mxd. Menu/UI/console scaling.
extern int ui_char_size;
extern int ui_scale;
extern int ui_line_height;
extern int ui_screen_width;
extern int ui_screen_offset_x;

extern cvar_t* scr_viewsize;
extern cvar_t* scr_centertime; //mxd
extern cvar_t* crosshair;

extern vrect_t scr_vrect; // Position of render window.

extern void SCR_Init(void);
extern void SCR_UpdateScreen(void);

extern void SCR_RunConsole(void);
extern void SCR_BeginLoadingPlaque(void);
extern void SCR_EndLoadingPlaque(void);
extern void SCR_UpdateProgressbar(int section); //mxd. Removed unused first parameter.
extern void SCR_UpdateUIScale(void); //mxd
extern void SCR_DebugGraph(float value, uint color); //mxd. Re-added.

extern void SCR_AddDirtyPoint(int x, int y);
extern void SCR_DirtyScreen(void);

// Level transition fade (cl_screen.c).
extern void SCR_StartLevelFade(qboolean fade_out); //mxd
extern qboolean SCR_IsFadingOut(void);             //mxd
extern void SCR_ClearLevelFade(void);              //mxd

// Starfield background (cl_screen.c).
extern void SCR_DrawStarfield(void);       //mxd
extern void SCR_UpdateLoadingScreen(void); //mxd. Call during loading to keep starfield animating.

// cl_smk.c
extern void SCR_PlayCinematic(const char *name);
extern void SCR_DrawCinematic(void); //mxd. Removed unused qboolean return type.
extern void SCR_RunCinematic(void);
extern void SCR_FinishCinematic(void);
extern void SCR_StopCinematic(void);
extern void SMK_Shutdown(void); //mxd