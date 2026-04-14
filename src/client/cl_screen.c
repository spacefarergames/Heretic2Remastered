//
// cl_screen.c -- master for refresh, status bar, console, chat, notify, etc
//
// Copyright 1998 Raven Software
//

#include "client.h"
#include "cl_messages.h"
#include "Vector.h"
#include "vid_dll.h"
#include "menus/menu_worldmap.h"

float scr_con_current; // Approaches scr_conlines at scr_conspeed.

static qboolean scr_initialized; // Ready to draw.

//mxd. Menu/UI/console scaling.
int ui_char_size = CONCHAR_SIZE;
int ui_scale = 1;
int ui_line_height = (int)((float)CONCHAR_SIZE * 1.25f); //mxd. Original logic uses 10.
int ui_screen_width = DEF_WIDTH; // Screen width sized to 4x3 aspect ratio.
int ui_screen_offset_x = 0; // Horizontal offset from viddef.width to centered ui_screen_width.

GameMessageDisplayInfo_t display_msg; //mxd

qboolean scr_draw_loading_plaque; // H2
static qboolean scr_draw_loading; // int in Q2
static int scr_progressbar_width; // H2

vrect_t scr_vrect; // Position of render window on screen.

cvar_t* scr_viewsize; //TODO: remove?
cvar_t* scr_centertime;
static cvar_t* scr_showturtle;
static cvar_t* scr_showpause;
static cvar_t* scr_printspeed;

static cvar_t* scr_netgraph;
static cvar_t* scr_timegraph;
static cvar_t* scr_debuggraph;
static cvar_t* scr_graphheight;
static cvar_t* scr_graphscale;
static cvar_t* scr_drawall;

// H2:
static cvar_t* scr_statbar;
static cvar_t* scr_item_paused;
static cvar_t* scr_item_loading;
static cvar_t* scr_text_fade_in;   //mxd. Duration of text fade-in effect in seconds.
static cvar_t* scr_text_fade_out;  //mxd. Duration of text fade-out effect in seconds.
static cvar_t* scr_level_fade_in;  //mxd. Duration of level fade-in effect in seconds.
static cvar_t* scr_level_fade_out; //mxd. Duration of level fade-out effect in seconds.
cvar_t* r_fog;
cvar_t* r_fog_density;

// Level transition fade state.
typedef enum
{
	LEVEL_FADE_NONE = 0,
	LEVEL_FADE_OUT,  // Fading to black before loading.
	LEVEL_FADE_IN    // Fading in from black after loading.
} level_fade_state_t;

static level_fade_state_t level_fade_state = LEVEL_FADE_NONE;
static int level_fade_start_time = 0;
static int level_fade_duration = 0;

//mxd. Loading screen map popup animation state.
static int loading_map_start_time = 0;
static float loading_map_scale = 0.0f;

//mxd. Loading screen starfield background.
#define STARFIELD_NUM_STARS		150
#define STARFIELD_TWINKLE_SPEED	0.003f
#define STARFIELD_DRIFT_SPEED	0.00002f

typedef struct
{
	float x;			// Normalized X position (0.0 to 1.0).
	float y;			// Normalized Y position (0.0 to 1.0).
	float dx;			// Drift velocity X (-1.0 to 1.0).
	float dy;			// Drift velocity Y (-1.0 to 1.0).
	float brightness;	// Base brightness (0.3 to 1.0).
	float phase;		// Twinkle phase offset (0.0 to 2*PI).
	float speed;		// Twinkle speed multiplier.
	int size;			// Star size (1 or 2 pixels).
} star_t;

static star_t loading_stars[STARFIELD_NUM_STARS];
static qboolean loading_stars_initialized = false;

//mxd. Northern lights (aurora borealis) effect.
#define AURORA_NUM_BANDS		5
#define AURORA_WAVE_SPEED		0.0003f
#define AURORA_DRIFT_SPEED		0.00005f

typedef struct
{
	float y_base;		// Base Y position (0.0 to 0.5, upper half of screen).
	float amplitude;	// Wave amplitude.
	float frequency;	// Wave frequency.
	float phase;		// Phase offset.
	float drift_speed;	// Horizontal drift speed.
	byte r, g, b;		// Base color.
	float alpha_max;	// Maximum alpha (0.0 to 1.0).
} aurora_band_t;

static aurora_band_t aurora_bands[AURORA_NUM_BANDS];

static void SCR_InitStarfield(void)
{
	// Use a fixed seed for consistent starfield between loads.
	unsigned int seed = 12345;

	for (int i = 0; i < STARFIELD_NUM_STARS; i++)
	{
		// Simple LCG random number generator.
		seed = seed * 1103515245 + 12345;
		loading_stars[i].x = (float)(seed & 0xFFFF) / 65535.0f;

		seed = seed * 1103515245 + 12345;
		loading_stars[i].y = (float)(seed & 0xFFFF) / 65535.0f;

		seed = seed * 1103515245 + 12345;
		loading_stars[i].brightness = 0.3f + 0.7f * (float)(seed & 0xFFFF) / 65535.0f;

		seed = seed * 1103515245 + 12345;
		loading_stars[i].phase = 6.28318f * (float)(seed & 0xFFFF) / 65535.0f;

		seed = seed * 1103515245 + 12345;
		loading_stars[i].speed = 0.5f + 1.5f * (float)(seed & 0xFFFF) / 65535.0f;

		seed = seed * 1103515245 + 12345;
		loading_stars[i].size = ((seed & 0xF) < 3) ? 2 : 1; // ~20% chance of larger star.

		// Random drift velocity (-1.0 to 1.0 range, will be scaled by STARFIELD_DRIFT_SPEED).
		seed = seed * 1103515245 + 12345;
		loading_stars[i].dx = ((float)(seed & 0xFFFF) / 32767.5f) - 1.0f;

		seed = seed * 1103515245 + 12345;
		loading_stars[i].dy = ((float)(seed & 0xFFFF) / 32767.5f) - 1.0f;
	}

	// Initialize aurora bands with varied colors (greens, blues, purples, pinks).
	// Aurora colors: typical northern lights range from green to blue to purple/pink.
	static const byte aurora_colors[AURORA_NUM_BANDS][3] =
	{
		{  50, 200,  80 },	// Green (most common aurora color).
		{  30, 180, 120 },	// Teal/cyan green.
		{  60, 120, 200 },	// Blue.
		{ 120,  80, 180 },	// Purple.
		{ 180,  60, 140 }	// Pink/magenta.
	};

	for (int i = 0; i < AURORA_NUM_BANDS; i++)
	{
		seed = seed * 1103515245 + 12345;
		aurora_bands[i].y_base = 0.05f + 0.25f * (float)(seed & 0xFFFF) / 65535.0f; // Upper portion of screen.

		seed = seed * 1103515245 + 12345;
		aurora_bands[i].amplitude = 0.02f + 0.04f * (float)(seed & 0xFFFF) / 65535.0f;

		seed = seed * 1103515245 + 12345;
		aurora_bands[i].frequency = 1.5f + 2.0f * (float)(seed & 0xFFFF) / 65535.0f;

		seed = seed * 1103515245 + 12345;
		aurora_bands[i].phase = 6.28318f * (float)(seed & 0xFFFF) / 65535.0f;

		seed = seed * 1103515245 + 12345;
		aurora_bands[i].drift_speed = 0.5f + 1.0f * (float)(seed & 0xFFFF) / 65535.0f;

		aurora_bands[i].r = aurora_colors[i][0];
		aurora_bands[i].g = aurora_colors[i][1];
		aurora_bands[i].b = aurora_colors[i][2];

		seed = seed * 1103515245 + 12345;
		aurora_bands[i].alpha_max = 0.15f + 0.15f * (float)(seed & 0xFFFF) / 65535.0f; // Subtle effect.
	}

	loading_stars_initialized = true;
}

void SCR_DrawStarfield(void)
{
	if (!loading_stars_initialized)
		SCR_InitStarfield();

	// Fill background with dark space color.
	const paletteRGBA_t bg_color = { .r = 2, .g = 2, .b = 8, .a = 255 };
	re.DrawFill(0, 0, viddef.width, viddef.height, bg_color);

	// Draw aurora borealis effect (northern lights).
	const float aurora_time = (float)curtime * AURORA_WAVE_SPEED;
	const float aurora_drift = (float)curtime * AURORA_DRIFT_SPEED;

	for (int band = 0; band < AURORA_NUM_BANDS; band++)
	{
		const aurora_band_t* aurora = &aurora_bands[band];

		// Draw aurora as wider vertical strips for better performance.
		const int strip_width = 16; // Wider strips = fewer draw calls.
		const int num_strips = (viddef.width + strip_width - 1) / strip_width;

		for (int strip = 0; strip < num_strips; strip++)
		{
			const float nx = (float)strip / (float)num_strips; // Normalized X (0 to 1).

			// Calculate wavy Y position with drift.
			const float wave_x = nx * aurora->frequency + aurora_drift * aurora->drift_speed + aurora->phase;
			const float wave_offset = sinf(wave_x * 6.28318f + aurora_time) * aurora->amplitude;
			const float y_pos = aurora->y_base + wave_offset;

			// Calculate pulsing alpha with variation across the band.
			const float pulse = 0.5f + 0.5f * sinf(aurora_time * 0.7f + aurora->phase + nx * 3.14159f);
			const float fade_at_edges = 1.0f - fabsf(nx - 0.5f) * 1.5f; // Fade toward screen edges.
			const float alpha = aurora->alpha_max * pulse * max(0.0f, fade_at_edges);

			if (alpha < 0.02f)
				continue;

			// Draw a few gradient segments instead of per-pixel (3 segments: top fade, center, bottom fade).
			const int x = strip * strip_width;
			const int base_y = (int)(y_pos * (float)viddef.height);
			const int half_height = (int)(30.0f * (float)ui_scale * (0.5f + 0.5f * pulse));

			// Draw 3 vertical segments with decreasing alpha.
			const int segment_height = max(1, half_height / 3);

			// Center segment (full alpha).
			if (base_y >= 0 && base_y < viddef.height)
			{
				const paletteRGBA_t color_center = { .r = aurora->r, .g = aurora->g, .b = aurora->b, .a = (byte)(alpha * 255.0f) };
				re.DrawFill(x, max(0, base_y - segment_height), strip_width, min(segment_height * 2, viddef.height - max(0, base_y - segment_height)), color_center);
			}

			// Top and bottom fade segments (reduced alpha).
			const byte fade_alpha = (byte)(alpha * 0.5f * 255.0f);
			if (fade_alpha > 5)
			{
				const paletteRGBA_t color_fade = { .r = aurora->r, .g = aurora->g, .b = aurora->b, .a = fade_alpha };

				// Top fade.
				const int top_y = base_y - segment_height * 2;
				if (top_y >= 0 && top_y < viddef.height)
					re.DrawFill(x, top_y, strip_width, segment_height, color_fade);

				// Bottom fade.
				const int bot_y = base_y + segment_height;
				if (bot_y >= 0 && bot_y < viddef.height)
					re.DrawFill(x, bot_y, strip_width, segment_height, color_fade);
			}

			// Outer fade (very low alpha).
			const byte outer_alpha = (byte)(alpha * 0.2f * 255.0f);
			if (outer_alpha > 3)
			{
				const paletteRGBA_t color_outer = { .r = aurora->r, .g = aurora->g, .b = aurora->b, .a = outer_alpha };

				const int outer_top = base_y - segment_height * 3;
				if (outer_top >= 0 && outer_top < viddef.height)
					re.DrawFill(x, outer_top, strip_width, segment_height, color_outer);

				const int outer_bot = base_y + segment_height * 2;
				if (outer_bot >= 0 && outer_bot < viddef.height)
					re.DrawFill(x, outer_bot, strip_width, segment_height, color_outer);
			}
		}
	}

	// Draw twinkling stars.
	const float time = (float)curtime * STARFIELD_TWINKLE_SPEED;
	const float drift_time = (float)curtime * STARFIELD_DRIFT_SPEED;

	for (int i = 0; i < STARFIELD_NUM_STARS; i++)
	{
		star_t* star = &loading_stars[i];

		// Calculate twinkle effect using sine wave.
		const float twinkle = 0.5f + 0.5f * sinf(time * star->speed + star->phase);
		const float alpha = star->brightness * (0.4f + 0.6f * twinkle);

		// Apply drift movement with wrapping.
		float px = star->x + star->dx * drift_time;
		float py = star->y + star->dy * drift_time;

		// Wrap positions to stay in 0-1 range.
		px = px - floorf(px);
		py = py - floorf(py);

		// Slight blue/white color variation based on brightness.
		const byte r = (byte)(180.0f + 75.0f * star->brightness);
		const byte g = (byte)(180.0f + 75.0f * star->brightness);
		const byte b = (byte)(200.0f + 55.0f * star->brightness);
		const paletteRGBA_t star_color = { .r = r, .g = g, .b = b, .a = (byte)(alpha * 255.0f) };

		const int x = (int)(px * (float)viddef.width);
		const int y = (int)(py * (float)viddef.height);

		re.DrawFill(x, y, star->size, star->size, star_color);
	}
}

typedef struct
{
	int x1;
	int y1;
	int x2;
	int y2;
} dirty_t;

static dirty_t scr_dirty;

typedef struct
{
	char* filename;
	int width;
} HudNumInfo_t; // H2

#pragma region ========================== BAR GRAPHS ==========================

// A new packet was just parsed.
void CL_AddNetgraph(void)
{
	// If using the debuggraph for something else, don't add the net lines.
	if ((int)scr_debuggraph->value || (int)scr_timegraph->value)
		return;

	SCR_DebugGraph((float)net_transmit_size, 0xfffffff);
}

typedef struct
{
	float value;
	paletteRGBA_t color; // Q2: int
} graphsamp_t;

static int current;
static graphsamp_t values[256]; // Q2: 1024

void SCR_DebugGraph(const float value, const uint color)
{
	const paletteRGBA_t pc = { .c = color };

	values[current & 255].value = fabsf(value);
	values[current & 255].color = pc;
	current++;
}

static void SCR_AddTimeGraph(void) // H2
{
	if ((int)scr_timegraph->value)
		SCR_DebugGraph((float)frame_index, 0xffffff);
}

static void SCR_DrawDebugGraph(void)
{
	static paletteRGBA_t c_bg = { .r = 64, .g = 64, .b = 64, .a = 255 }; //mxd

	if (!(int)scr_debuggraph->value && !(int)scr_timegraph->value && !(int)scr_netgraph->value)
		return;

	const int x = (viddef.width - 256) / 2;
	const int y = viddef.height - 8;
	
	re.DrawFill(x, viddef.height - (int)scr_graphheight->value - 8, 256, (int)scr_graphheight->value, c_bg);

	for (int i = 0; i < 256; i++)
	{
		const int index = (current - i - 1) & 255;
		const int value = ClampI((int)(values[index].value * scr_graphscale->value), 0, (int)scr_graphheight->value);

		re.DrawFill(x + 255 - i, y - value, 1, value, values[index].color);
	}

	re.DrawFill(x, y - (int)(scr_graphscale->value * 100.0f), 256, 1, TextPalette[P_GREEN]);
	re.DrawFill(x, y - (int)(scr_graphscale->value * 150.0f), 256, 1, TextPalette[P_YELLOW]);
	re.DrawFill(x, y - (int)(scr_graphscale->value * 200.0f), 256, 1, TextPalette[P_RED]);
}

#pragma endregion

// Sets scr_vrect, the coordinates of the rendered window.
static void SCR_CalcVrect(void)
{
	// Bound viewsize
	if (scr_viewsize->value < 30.0f) // 40 in Q2
		Cvar_Set("viewsize", "30");

	if (scr_viewsize->value > 100.0f)
		Cvar_Set("viewsize", "100");

	const int size = (int)scr_viewsize->value;

	scr_vrect.width = viddef.width * size / 100;
	scr_vrect.width &= ~1; // Reduce to make even --mxd. //mxd. ~7 (reduce to make power of 8) in original logic. Fixes unrendered areas when window width is not power of 8 (like 1366x768).

	scr_vrect.height = viddef.height * size / 100;
	scr_vrect.height &= ~1; // Reduce to make even --mxd.

	scr_vrect.x = (viddef.width - scr_vrect.width) / 2;
	scr_vrect.y = (viddef.height - scr_vrect.height) / 2;
}

// Q2 counterpart
static void SCR_TimeRefresh_f(void)
{
	if (cls.state != ca_active)
		return;

	const int starttime = curtime; //mxd. Sys_Milliseconds() -> curtime.

	if (Cmd_Argc() == 2)
	{
		// Run without page flipping.
		re.BeginFrame(0.0f);

		for (int i = 0; i < 128; i++)
		{
			cl.refdef.viewangles[1] = (float)i / 128.0f * 360.0f;
			re.RenderFrame(&cl.refdef);
		}

		re.EndFrame();
	}
	else
	{
		// Run with page flipping.
		for (int i = 0; i < 128; i++)
		{
			cl.refdef.viewangles[1] = (float)i / 128.0f * 360.0f;

			re.BeginFrame(0.0f);
			re.RenderFrame(&cl.refdef);
			re.EndFrame();
		}
	}

	const float time = (float)(curtime - starttime) / 1000.0f; //mxd. Sys_Milliseconds() -> curtime.
	Com_Printf("%f seconds (%f fps)\n", time, 128.0f / time);
}

static void SCR_Loading_f(void)
{
	SCR_BeginLoadingPlaque();
}

// Q2 counterpart
static void SCR_SizeUp_f(void) //TODO: remove?
{
	Cvar_SetValue("viewsize", scr_viewsize->value + 10.0f);
}

// Q2 counterpart
static void SCR_SizeDown_f(void) //TODO: remove?
{
	Cvar_SetValue("viewsize", scr_viewsize->value - 10.0f);
}

// Q2 counterpart
// Set a specific sky and rotation speed.
static void SCR_Sky_f(void)
{
	float rotate;
	vec3_t axis;

	if (Cmd_Argc() < 2)
	{
		Com_Printf("Usage: sky <basename> <rotate> <axis x y z>\n");
		return;
	}

	if (Cmd_Argc() > 2)
		rotate = (float)strtod(Cmd_Argv(2), NULL); //mxd. atof -> strtod
	else
		rotate = 0.0f;

	if (Cmd_Argc() == 6)
	{
		axis[0] = (float)strtod(Cmd_Argv(3), NULL); //mxd. atof -> strtod
		axis[1] = (float)strtod(Cmd_Argv(4), NULL); //mxd. atof -> strtod
		axis[2] = (float)strtod(Cmd_Argv(5), NULL); //mxd. atof -> strtod
	}
	else
	{
		VectorSet(axis, 0, 0, 1);
	}

	re.SetSky(Cmd_Argv(1), rotate, axis);
}

static void SCR_GammaUp_f(void) // H2. Actually decreases brightness.
{
	if (vid_gamma->value < 4.0f)
		Cvar_SetValue("vid_gamma", vid_gamma->value + 0.1f);
}

static void SCR_GammaDown_f(void) // H2. Actually increases brightness.
{
	if (vid_gamma->value > 0.1f)
		Cvar_SetValue("vid_gamma", vid_gamma->value - 0.1f);
}

void SCR_RunConsole(void)
{
	scr_con_current = ((cls.key_dest == key_console) ? 0.5f : 0.0f);
}

static void SCR_DrawConsole(void)
{
	Con_CheckResize();

	if (cls.state == ca_disconnected || cls.state == ca_connecting)
	{
		// Forced fullscreen console.
		Con_DrawConsole(1.0f);
	}
	else if (cls.state != ca_active || !cl.refresh_prepped)
	{
		// Connected, but can't render.
		Con_DrawConsole(1.0f);
		SCR_DirtyScreen(); // H2
	}
	else if (scr_con_current != 0.0f)
	{
		Con_DrawConsole(scr_con_current);
	}
	else if (cls.key_dest == key_game || cls.key_dest == key_message)
	{
		// Only draw notify in game.
		Con_DrawNotify();
	}
}

void SCR_BeginLoadingPlaque(void)
{
	se.StopAllSounds_Sounding();
	se.MusicStop(); //mxd. CDAudio_Stop() in original logic.
	cl.sound_prepped = false; // Don't play ambients.

	scr_draw_loading_plaque = true; // H2

	if (!cls.disable_screen && !(int)developer->value && cls.key_dest != key_console)
	{
		if (cl.cinematictime == 0)
			scr_draw_loading = true;

		scr_progressbar_width = 0; // H2

		//mxd. Start fade-out effect before loading.
		SCR_StartLevelFade(true);

		// Render fade-out frames for smooth transition.
		while (SCR_IsFadingOut() && (cls.realtime - level_fade_start_time) < level_fade_duration)
		{
			IN_Update(); // Pump message loop.
			curtime = (int)(Sys_Microseconds() / 1000ll); // Update curtime.
			cls.realtime = curtime;
			SCR_UpdateScreen();
		}

		// Clear fade state so loading screen is visible.
		SCR_ClearLevelFade();

		SCR_UpdateScreen();
		cls.disable_screen = true; // Q2: Sys_Milliseconds()
		cls.disable_servercount = cl.servercount;
	}
}

void SCR_EndLoadingPlaque(void)
{
	cls.disable_screen = false;
	scr_draw_loading_plaque = false; // H2
	scr_draw_loading = false; // H2
	loading_map_start_time = 0; //mxd. Reset map popup animation state.
	Con_ClearNotify();

	//mxd. Start fade-in effect after loading completes.
	SCR_StartLevelFade(false);
}

//mxd. Removed unused first parameter.
void SCR_UpdateProgressbar(const int section) // H2
{
#define NUM_PROGRESSBAR_SECTIONS	6

	const int w = ClampI(section, 0, NUM_PROGRESSBAR_SECTIONS);
	scr_progressbar_width = (w << NUM_PROGRESSBAR_SECTIONS) / NUM_PROGRESSBAR_SECTIONS;

	SCR_UpdateLoadingScreen();
}

//mxd. Update loading screen with message pumping to keep starfield animating and window responsive.
void SCR_UpdateLoadingScreen(void)
{
	if (!scr_draw_loading)
		return;

	// Temporarily enable screen updates.
	const qboolean was_disabled = cls.disable_screen;
	cls.disable_screen = false;

	IN_Update(); // Pump message loop to keep window responsive.
	curtime = (int)(Sys_Microseconds() / 1000ll); // Update curtime for starfield animation.
	cls.realtime = curtime;

	SCR_UpdateScreen();

	cls.disable_screen = was_disabled;
}

//mxd. Expected to be called when screen size changes.
void SCR_UpdateUIScale(void)
{
	ui_scale = min((int)(roundf((float)viddef.width / DEF_WIDTH)), (int)(roundf((float)viddef.height / DEF_HEIGHT)));
	ui_char_size = CONCHAR_SIZE * ui_scale;
	ui_line_height = (int)((float)ui_char_size * 1.25f);

	if ((float)viddef.width * 0.75f > (float)viddef.height) // Setup for widescreen aspect ratio.
	{
		ui_screen_width = viddef.height * 4 / 3;
		ui_screen_offset_x = (viddef.width - ui_screen_width) / 2;
	}
	else
	{
		ui_screen_width = viddef.width;
		ui_screen_offset_x = 0;
	}
}

void SCR_Init(void)
{
	scr_viewsize = Cvar_Get("viewsize", "100", CVAR_ARCHIVE);
	scr_showturtle = Cvar_Get("scr_showturtle", "0", 0);
	scr_showpause = Cvar_Get("scr_showpause", "1", 0);
	scr_centertime = Cvar_Get("scr_centertime", "2.5", 0);
	scr_printspeed = Cvar_Get("scr_printspeed", "8", 0);
	scr_netgraph = Cvar_Get("scr_netgraph", "0", 0);
	scr_timegraph = Cvar_Get("scr_timegraph", "0", 0);
	scr_debuggraph = Cvar_Get("scr_debuggraph", "0", 0);
	scr_graphheight = Cvar_Get("scr_graphheight", "112", 0);
	scr_graphscale = Cvar_Get("scr_graphscale", "0.2", 0); // H2_1.07: "0.2" -> "1".
	scr_drawall = Cvar_Get("scr_drawall", "0", 0);

	// H2:
	scr_statbar = Cvar_Get("scr_statbar", "1", 0);
	scr_item_paused = Cvar_Get("scr_item_paused", "Paused", 0);
	scr_item_loading = Cvar_Get("scr_item_loading", "Loading", 0);
	scr_text_fade_in = Cvar_Get("scr_text_fade_in", "0.3", 0);   //mxd. 300ms default fade-in.
	scr_text_fade_out = Cvar_Get("scr_text_fade_out", "0.3", 0); //mxd. 300ms default fade-out.
	scr_level_fade_in = Cvar_Get("scr_level_fade_in", "0.5", CVAR_ARCHIVE);  //mxd. 500ms default level fade-in.
	scr_level_fade_out = Cvar_Get("scr_level_fade_out", "0.3", CVAR_ARCHIVE); //mxd. 300ms default level fade-out.
	r_fog = Cvar_Get("r_fog", "0", 0);
	r_fog_density = Cvar_Get("r_fog_density", "0", 0);
	//gl_lostfocus_broken = Cvar_Get("gl_lostfocus_broken", "0", 0); //mxd. Ignored

	// Register our commands
	Cmd_AddCommand("timerefresh", SCR_TimeRefresh_f);
	Cmd_AddCommand("loading", SCR_Loading_f);
	Cmd_AddCommand("sizeup", SCR_SizeUp_f);
	Cmd_AddCommand("sizedown", SCR_SizeDown_f);
	Cmd_AddCommand("sky", SCR_Sky_f);
	Cmd_AddCommand("vid_gamma_up", SCR_GammaUp_f);
	Cmd_AddCommand("vid_gamma_down", SCR_GammaDown_f);

	scr_initialized = true;
}

static void SCR_DrawNet(void)
{
	if (cls.netchan.outgoing_sequence - cls.netchan.incoming_acknowledged >= CMD_BACKUP - 1)
	{
		const int offset = ui_scale * 16;
		re.DrawPic(scr_vrect.x + offset, scr_vrect.y + offset, ui_scale, "misc/net.m8", 1.0f); // Q2: re.DrawPic(scr_vrect.x + 64, scr_vrect.y, "net");
	}
}

static void SCR_DrawPause(void)
{
	if (!(int)scr_showpause->value || !CL_PAUSED || cls.key_dest == key_console) // Turn off for screenshots. //mxd. Also turn off when console is opened.
		return;

	if (Cvar_VariableInt("maxclients") < 2 && Com_ServerState())
	{
		char pause_pic[MAX_QPATH];
		Com_sprintf(pause_pic, sizeof(pause_pic), "\x03%s", scr_item_paused->string);

		const int w = re.BF_Strlen(pause_pic);
		const int x = (DEF_WIDTH - w) / 2;

		re.DrawStretchPic(x - 16, 48, w + 32, 48, "misc/textback.m32", 1.0f, DSP_SCALE_4x3);
		re.DrawBigFont(x, 80, pause_pic, 1.0f);
	}
	else
	{
		Cvar_SetValue("paused", 0);
	}
}

static void SCR_DrawLoading(void)
{
	if (!scr_draw_loading || strcmp(cls.servername, "localhost") != 0 || Cvar_IsSet("coop") || Cvar_IsSet("deathmatch"))
		return;

	// Draw twinkling starfield background (fills widescreen letterbox areas).
	SCR_DrawStarfield();

	// Tell Menu_DrawBG to skip black fill since starfield is already drawn.
	m_skip_bg_fill = true;

	// Animate map popup zoom-in effect (similar to pause menu).
	if (loading_map_start_time == 0)
		loading_map_start_time = curtime;

	const float elapsed = (float)(curtime - loading_map_start_time);
	const float duration = 250.0f; // Same duration as menu zoom.
	loading_map_scale = min(elapsed / duration, 1.0f);

	// Draw map bg with zoom animation.
	cls.m_menuscale = loading_map_scale;
	cls.m_menualpha = loading_map_scale;

	M_WorldMap_MenuDraw();

	cls.m_menuscale = 0.0f;
	cls.m_menualpha = 0.0f;
	m_skip_bg_fill = false;

	char label[MAX_QPATH];
	Com_sprintf(label, sizeof(label), "\x03%s", scr_item_loading->string);

	const int w = re.BF_Strlen(label);
	const int x = (DEF_WIDTH - w) >> 1;

	// Draw label.
	re.DrawStretchPic(x - 16, 48, w + 32, 48, "misc/textback.m32", 1.0f, DSP_SCALE_4x3);
	re.DrawBigFont(x, 80, label, 1.0f);

	// Draw progressbar.
	re.DrawStretchPic(49, 432, 70, 16, "icons/breath2.m8", 1.0f, DSP_SCALE_4x3);
	re.DrawStretchPic(52, 432, scr_progressbar_width, 16, "icons/breath.m8", 1.0f, DSP_SCALE_4x3);
}

// Q2 counterpart
void SCR_AddDirtyPoint(const int x, const int y)
{
	scr_dirty.x1 = min(x, scr_dirty.x1);
	scr_dirty.x2 = max(x, scr_dirty.x2);
	scr_dirty.y1 = min(y, scr_dirty.y1);
	scr_dirty.y2 = max(y, scr_dirty.y2);
}

// Q2 counterpart
void SCR_DirtyScreen(void)
{
	SCR_AddDirtyPoint(0, 0);
	SCR_AddDirtyPoint(viddef.width - 1, viddef.height - 1);
}

//TODO: remove - irrelevant in OpenGL
// Clear any parts of the tiled background that were drawn on last frame
static void SCR_TileClear(void)
{
	static dirty_t scr_old_dirty[2]; //mxd. Made static

	// For power vr or broken page flippers...
	if ((int)scr_drawall->value)
		SCR_DirtyScreen();

	// Skip when fullscreen console, rendering or cinematic.
	if (scr_con_current == 1.0f || scr_viewsize->value == 100.0f || cl.cinematictime > 0) //TODO: scr_con_current is never set to 1.0f.
		return;

	// Erase rect will be the union of the past three frames so triple buffering works properly
	dirty_t clear = scr_dirty;
	for (int i = 0; i < 2; i++)
	{
		if (scr_old_dirty[i].x1 < clear.x1)
			clear.x1 = scr_old_dirty[i].x1;
		if (scr_old_dirty[i].x2 > clear.x2)
			clear.x2 = scr_old_dirty[i].x2;
		if (scr_old_dirty[i].y1 < clear.y1)
			clear.y1 = scr_old_dirty[i].y1;
		if (scr_old_dirty[i].y2 > clear.y2)
			clear.y2 = scr_old_dirty[i].y2;
	}

	scr_old_dirty[1] = scr_old_dirty[0];
	scr_old_dirty[0] = scr_dirty;

	scr_dirty.x1 = 9999;
	scr_dirty.x2 = -9999;
	scr_dirty.y1 = 9999;
	scr_dirty.y2 = -9999;

	// Don't bother with anything covered by the console
	const int console_top = (int)(scr_con_current * (float)viddef.height);
	if (console_top >= clear.y1)
		clear.y1 = console_top;

	if (clear.y2 <= clear.y1)
		return; // Nothing disturbed

	const int top = scr_vrect.y;
	const int bottom = top + scr_vrect.height - 1;
	const int left = scr_vrect.x;
	const int right = left + scr_vrect.width - 1;

	// Clear above view screen?
	if (clear.y1 < top)
	{
		const int i = (clear.y2 < top - 1 ? clear.y2 : top - 1);
		re.DrawTileClear(clear.x1, clear.y1, clear.x2 - clear.x1 + 1, i - clear.y1 + 1, "misc/backtile.m8"); // H2: backtile -> misc/backtile.m8
		clear.y1 = top;
	}

	// Clear below view screen?
	if (clear.y2 > bottom)
	{
		const int i = (clear.y1 > bottom + 1 ? clear.y1 : bottom + 1);
		re.DrawTileClear(clear.x1, i, clear.x2 - clear.x1 + 1, clear.y2 - i + 1, "misc/backtile.m8"); // H2: backtile -> misc/backtile.m8
		clear.y2 = bottom;
	}

	// Clear left of view screen?
	if (clear.x1 < left)
	{
		const int i = (clear.x2 < left - 1 ? clear.x2 : left - 1);
		re.DrawTileClear(clear.x1, clear.y1, i - clear.x1 + 1, clear.y2 - clear.y1 + 1, "misc/backtile.m8"); // H2: backtile -> misc/backtile.m8
		clear.x1 = left;
	}

	// Clear left of view screen?
	if (clear.x2 > right)
	{
		const int i = (clear.x1 > right + 1 ? clear.x1 : right + 1);
		re.DrawTileClear(i, clear.y1, clear.x2 - i + 1, clear.y2 - clear.y1 + 1, "misc/backtile.m8"); // H2: backtile -> misc/backtile.m8
		//clear.x2 = right;
	}
}

static void DrawPic(const int x, const int y, char* str, const qboolean use_alpha) // H2
{
	const int img_data = cl.frame.playerstate.stats[Q_atoi(COM_Parse(&str))];

	if (img_data == 0)
		return;

	const qboolean img_no_alpha = (img_data & 0x8000);
	const int img_index = (img_data & 0x7fff);

	float alpha = 1.0f;
	if (use_alpha && !img_no_alpha)
		alpha = 0.5f;

	const char* img_name = cl.configstrings[CS_IMAGES + img_index];

	if (*img_name != 0)
	{
		SCR_AddDirtyPoint(x, y);
		SCR_AddDirtyPoint(x + 31 * ui_scale, y + 31 * ui_scale);
		re.DrawPic(x, y, ui_scale, img_name, alpha);
	}
}

static void DrawTeamBlock(int x, int y, char* str) // H2 //TODO: 'x' and 'y' args are ignored.
{
	int ox = Q_atoi(COM_Parse(&str));
	ox += (ui_screen_width / 2 - 128 * ui_scale) + ui_screen_offset_x;

	int oy = Q_atoi(COM_Parse(&str));
	oy += viddef.height / 2 - 120 * ui_scale;

	//TODO: needs the same 'oy' adjustment as in DrawClientBlock(). How do we get team index? Is this even used?
	const int score = Q_atoi(COM_Parse(&str));
	const char* team = COM_Parse(&str);

	DrawString(ox, oy, va("Team %s", team), TextPalette[P_TEAM], -1);
	DrawString(ox, oy + ui_char_size, va("Score %i", score), TextPalette[P_WHITE], -1);
}

static void DrawClientBlock(int x, int y, char* str) // H2 //TODO: 'x' and 'y' args are ignored.
{
	int ox = Q_atoi(COM_Parse(&str));
	ox += (ui_screen_width / 2 - 160 * ui_scale) + ui_screen_offset_x;

	int oy = Q_atoi(COM_Parse(&str));
	oy += viddef.height / 2 - 120 * ui_scale;

	const int client = Q_atoi(COM_Parse(&str));

	//mxd. Gross hacks: adjust 'oy' to match UI scaling by adding missing part of char height for 4 lines...
	// Doing this correctly (by adjusting xy coords in DeathmatchScoreboardMessage()) will break vanilla compatibility...
	oy += client * (ui_char_size - CONCHAR_SIZE) * 4;

	SCR_AddDirtyPoint(ox, oy);
	SCR_AddDirtyPoint(ox + 159 * ui_scale, oy + 31 * ui_scale);

	if (client < 0 || client >= MAX_CLIENTS)
		Com_Error(ERR_DROP, "client >= MAX_CLIENTS");

	const int score = Q_atoi(COM_Parse(&str));
	const int ping = Q_atoi(COM_Parse(&str));
	const int time = Q_atoi(COM_Parse(&str));

	DrawString(ox + ui_char_size * 4, oy, cl.clientinfo[client].name, TextPalette[P_FRAGNAME], -1);

	DrawString(ox + ui_char_size * 4,  oy + ui_char_size, "Score: ", TextPalette[P_FRAGS], -1);
	DrawString(ox + ui_char_size * 11, oy + ui_char_size, va("%i", score), TextPalette[P_ALTFRAGS], -1);
	DrawString(ox + ui_char_size * 4,  oy + ui_char_size * 2, va("Ping:  %i", ping), TextPalette[P_FRAGS], -1);
	DrawString(ox + ui_char_size * 4,  oy + ui_char_size * 3, va("Time:  %i", time), TextPalette[P_FRAGS], -1);
}

static void DrawAClientBlock(int x, int y, char* str) // H2 //TODO: 'x' and 'y' args are ignored.
{
	char buffer[80];

	int ox = Q_atoi(COM_Parse(&str));
	ox += viddef.width / 2 - 160 * ui_scale;

	int oy = Q_atoi(COM_Parse(&str));
	oy += viddef.height / 2 - 120 * ui_scale;

	SCR_AddDirtyPoint(ox, oy);
	SCR_AddDirtyPoint(ox + 159 * ui_scale, oy + 31 * ui_scale);

	const int pal_index = Q_atoi(COM_Parse(&str));
	const int client = Q_atoi(COM_Parse(&str));

	if (client < 0 || client >= MAX_CLIENTS)
		Com_Error(ERR_DROP, "client >= MAX_CLIENTS");

	const int score = Q_atoi(COM_Parse(&str));
	const int ping = Q_atoi(COM_Parse(&str));
	const int time = Q_atoi(COM_Parse(&str));

	sprintf_s(buffer, sizeof(buffer), "%-12.12s    %3d   %3d   %3d", cl.clientinfo[client].name, score, ping, time); //mxd. sprintf -> sprintf_s
	DrawString(ox, oy, buffer, TextPalette[pal_index], -1);
}

static int GetMenuNumsIndex(const char c, const qboolean is_red) // H2
{
	if (c == '-')
		return 20;

	const int offset = (is_red ? 10 : 0);
	return c + offset - '0';
}

static void DrawHudNum(const int x, const int y, int width, const int value, const qboolean is_red)
{
	static HudNumInfo_t menu_nums[] =
	{
		{ "menu/num_0.m32",		8 },
		{ "menu/num_1.m32",		8 },
		{ "menu/num_2.m32",		8 },
		{ "menu/num_3.m32",		8 },
		{ "menu/num_4.m32",		8 },
		{ "menu/num_5.m32",		8 },
		{ "menu/num_6.m32",		8 },
		{ "menu/num_7.m32",		8 },
		{ "menu/num_8.m32",		8 },
		{ "menu/num_9.m32",		8 },

		{ "menu/num_red0.m32",	8 },
		{ "menu/num_red1.m32",	8 },
		{ "menu/num_red2.m32",	8 },
		{ "menu/num_red3.m32",	8 },
		{ "menu/num_red4.m32",	8 },
		{ "menu/num_red5.m32",	8 },
		{ "menu/num_red6.m32",	8 },
		{ "menu/num_red7.m32",	8 },
		{ "menu/num_red8.m32",	8 },
		{ "menu/num_red9.m32",	8 },

		{ "menu/num_neg.m32",	8 }
	};

	if (width <= 0)
		return;

	width = min(3, width);

	char num[16];
	Com_sprintf(num, sizeof(num), "%i", value);

	const int len = min(width, (int)strlen(num));
	int draw_width = 0;

	for (int i = 0; i < len; i++)
	{
		const int num_index = GetMenuNumsIndex(num[i], is_red);
		draw_width += menu_nums[num_index].width;
	}

	int ox = x - 30 * ui_scale + (width * 19 - draw_width) * ui_scale;

	SCR_AddDirtyPoint(ox, y + 16 * ui_scale);
	SCR_AddDirtyPoint(ox + draw_width * ui_scale, y + 24 * ui_scale);

	for (int i = 0; i < len; i++)
	{
		const int num_index = GetMenuNumsIndex(num[i], is_red);
		re.DrawPic(ox, y + 16 * ui_scale, ui_scale, menu_nums[num_index].filename, 1.0f);
		ox += menu_nums[num_index].width * ui_scale;
	}
}

static void DrawBar(const int x, const int y, int width, const int height, const int stat_index) // H2
{
	const short bar_index = cl.frame.playerstate.stats[stat_index];
	const short bg_index = cl.frame.playerstate.stats[stat_index + 1];
	short scaler = cl.frame.playerstate.stats[stat_index + 2];

	SCR_AddDirtyPoint(x, y - 3 * ui_scale);
	SCR_AddDirtyPoint(x + width * ui_scale, y + (height + 3) * ui_scale);

	const char* bar_name = cl.configstrings[CS_IMAGES + bar_index];
	const char* bg_name = cl.configstrings[CS_IMAGES + bg_index];

	if (*bar_name == 0)
		return;

	if (scaler < 0)
	{
		scaler *= -1;
		width *= 2;
	}

	if (width < height)
	{
		if (*bg_name != 0)
			re.DrawStretchPic(x, y - 3 * ui_scale, width * ui_scale, (height + 6) * ui_scale, bg_name, 1.0f, DSP_NONE);

		const int offset = (int)((float)height - (float)(height * scaler) * 0.01f);
		re.DrawStretchPic(x, y + offset * ui_scale, width * ui_scale, (height - offset) * ui_scale, bar_name, 1.0f, DSP_NONE);
	}
	else
	{
		if (*bg_name != 0)
			re.DrawStretchPic(x - 3 * ui_scale, y, (width + 6) * ui_scale, height * ui_scale, bg_name, 1.0f, DSP_NONE);

		const int offset = (int)((float)width - (float)(width * scaler) * 0.01f);
		re.DrawStretchPic(x, y, (width - offset) * ui_scale, height * ui_scale, bar_name, 1.0f, DSP_NONE);
	}
}

static void SCR_ExecuteLayoutString(char* s)
{
	if (cls.state != ca_active || !cl.refresh_prepped || s[0] == 0)
		return;

	int x = 0;
	int y = 0;
	int width = 3;

	while (s != NULL)
	{
		const char* token = COM_Parse(&s);

		if (strcmp(token, "xl") == 0)
		{
			x = Q_atoi(COM_Parse(&s)) * ui_scale;
		}
		else if (strcmp(token, "xr") == 0)
		{
			x = viddef.width + Q_atoi(COM_Parse(&s)) * ui_scale;
		}
		else if (strcmp(token, "xv") == 0)
		{
			x = viddef.width / 2 - (160 + Q_atoi(COM_Parse(&s))) * ui_scale;
		}
		else if (strcmp(token, "xc") == 0) // H2
		{
			const int offset = cl.frame.playerstate.stats[STAT_PUZZLE_COUNT] * 40 * ui_scale;
			x = (viddef.width - offset) / 2 + Q_atoi(COM_Parse(&s)) * ui_scale;
		}
		else if (strcmp(token, "yt") == 0)
		{
			y = Q_atoi(COM_Parse(&s)) * ui_scale;
		}
		else if (strcmp(token, "yb") == 0)
		{
			y = viddef.height + Q_atoi(COM_Parse(&s)) * ui_scale;
		}
		else if (strcmp(token, "yv") == 0)
		{
			y = viddef.height / 2 - (120 + Q_atoi(COM_Parse(&s))) * ui_scale;
		}
		else if (strcmp(token, "yp") == 0) // H2
		{
			y += Q_atoi(COM_Parse(&s)) * ui_scale;
		}
		else if (strcmp(token, "pic") == 0)
		{
			// Draw a pic from a stat number.
			DrawPic(x, y, s, false);
		}
		else if (strcmp(token, "pici") == 0) // H2
		{
			// When 'Show puzzle inventory' flag is set.
			if ((cl.frame.playerstate.stats[STAT_LAYOUTS] & 4) != 0)
				DrawPic(x, y, s, true);
		}
		else if (strcmp(token, "tm") == 0) // H2
		{
			// Draw a team deathmatch team block.
			DrawTeamBlock(x, y, s);
		}
		else if (strcmp(token, "client") == 0)
		{
			// Draw a deathmatch client block.
			DrawClientBlock(x, y, s);
		}
		else if (strcmp(token, "aclient") == 0) // H2 //TODO: never used?
		{
			// Draw a coop client block (?).
			DrawAClientBlock(x, y, s);
		}
		else if (strcmp(token, "picn") == 0)
		{
			// Draw a pic from a name.
			SCR_AddDirtyPoint(x, y);
			SCR_AddDirtyPoint(x + 32 * ui_scale, y + 32 * ui_scale);

			re.DrawPic(x, y, ui_scale, COM_Parse(&s), 1.0f);
		}
		else if (strcmp(token, "num") == 0)
		{
			// Draw a number.
			width = Q_atoi(COM_Parse(&s));
			const int value = cl.frame.playerstate.stats[Q_atoi(COM_Parse(&s))];

			DrawHudNum(x, y, width, value, value <= 0);
		}
		else if (strcmp(token, "hnum") == 0) // H2
		{
			// Draw health number.
			const int amount = max(-99, cl.frame.playerstate.stats[STAT_HEALTH]);
			DrawHudNum(x, y, width, amount, amount <= 25);
		}
		else if (strcmp(token, "arm") == 0) // H2
		{
			// Draw armor number.
			if (cl.frame.playerstate.stats[STAT_ARMOUR] != 0)
			{
				const int amount = max(-99, cl.frame.playerstate.stats[STAT_ARMOUR]);
				DrawHudNum(x, y, width, amount, amount <= 25);
			}
		}
		else if (strcmp(token, "am") == 0) // H2
		{
			// Draw ammo number.
			if (cl.frame.playerstate.stats[STAT_AMMO_ICON] != 0)
			{
				const int amount = max(0, cl.frame.playerstate.stats[STAT_AMMO]);
				DrawHudNum(x, y, width, amount, amount <= 10);
			}
		}
		else if (strcmp(token, "bar") == 0) // H2
		{
			const int stat_index = Q_atoi(COM_Parse(&s));
			width = Q_atoi(COM_Parse(&s));
			const int height = Q_atoi(COM_Parse(&s));

			DrawBar(x, y, width, height, stat_index);
		}
		else if (strcmp(token, "gbar") == 0) // H2
		{
			const int index = Q_atoi(COM_Parse(&s));
			if (index < 0 || index >= MAX_STATS - 1) //mxd. Add sanity check.
				Com_Error(ERR_DROP, "Bad gbar index");

			width = cl.frame.playerstate.stats[index];
			const int height = cl.frame.playerstate.stats[index + 1];

			DrawBar(x, y, width, height, index + 2);
		}
		else if (strcmp(token, "stat_string") == 0)
		{
			int index = Q_atoi(COM_Parse(&s));
			if (index < 0 || index >= MAX_STATS) //mxd. '>= MAX_CONFIGSTRINGS' in original logic.
				Com_Error(ERR_DROP, "Bad stat_string index");

			index = cl.frame.playerstate.stats[index];
			if (index < 0 || index >= MAX_CONFIGSTRINGS)
				Com_Error(ERR_DROP, "Bad stat_string index");

			DrawString(x, y, cl.configstrings[index], TextPalette[P_WHITE], -1);
		}
		else if (strcmp(token, "hstring") == 0) // H2
		{
			x = viddef.width / 2 - (160 + Q_atoi(COM_Parse(&s))) * ui_scale;
			y = viddef.height / 2 - (120 + Q_atoi(COM_Parse(&s))) * ui_scale;
			const int pal_index = Q_atoi(COM_Parse(&s));
			const char* str = COM_Parse(&s);

			DrawString(x, y, str, TextPalette[pal_index], -1);
		}
		else if (strcmp(token, "string") == 0)
		{
			x = Q_atoi(COM_Parse(&s));
			y = Q_atoi(COM_Parse(&s));
			const int pal_index = Q_atoi(COM_Parse(&s));
			const char* str = COM_Parse(&s);

			DrawString(x, y, str, TextPalette[pal_index], -1);
		}
		else if (strcmp(token, "if") == 0)
		{
			// Draw a number.
			token = COM_Parse(&s);
			if (cl.frame.playerstate.stats[Q_atoi(token)] == 0)
				while (s != NULL && strcmp(token, "endif") != 0) // Skip to endif.
					token = COM_Parse(&s);
		}
	}
}

// The status bar is a small layout program that is based on the stats array.
static void SCR_DrawStats(void)
{
	if ((int)scr_statbar->value && !CL_IgnoreInput()) // H2: extra checks //mxd. Skip when looking through remote camera.
		SCR_ExecuteLayoutString(cl.configstrings[CS_STATUSBAR]);
}

static void SCR_DrawLayout(void)
{
	if ((cl.frame.playerstate.stats[STAT_LAYOUTS] & 1) != 0) // H2: proceed when 'Show scores' flag is set.
		SCR_ExecuteLayoutString(cl.layout);
}

static void SCR_DrawNames(void) // H2
{
	if (!(int)shownames->value)
		return;

	for (int i = 0; i < Q_atoi(cl.configstrings[CS_MAXCLIENTS]); i++)
	{
		// Skip disconnected players (see ClientDisconnect()).
		if (cl.configstrings[CS_PLAYERSKINS + i][0] == 0)
			continue;

		// Not in player's view.
		if ((cl.PIV & cl.frame.playerstate.PIV & (1 << i)) == 0)
			continue;

		paletteRGBA_t color;

		if (cl.frame.playerstate.dmflags & DF_SKINTEAMS)
		{
			if (Q_stricmp(cl.clientinfo[i].skin_name, cl.clientinfo[cl.playernum].skin_name) == 0)
				color = TextPalette[P_GREEN];
			else
				color = TextPalette[P_RED];
		}
		else if (cl.frame.playerstate.dmflags & DF_MODELTEAMS)
		{
			if (Q_stricmp(cl.clientinfo[i].model_name, cl.clientinfo[cl.playernum].model_name) == 0)
				color = TextPalette[P_GREEN];
			else
				color = TextPalette[P_RED];
		}
		else
		{
			color = TextPalette[COLOUR(colour_names)];
		}

		const vec3_t origin = VEC3_INITA(cl.clientinfo[i].origin, 0.0f, 0.0f, 32.0f); //mxd. '+= 64.0f - shownames->value * 32.0f' in original logic.
		re.Draw_Name(origin, cl.clientinfo[i].name, color);
	}
}

#define CINE_BORDER_TOP_HEIGHT		((viddef.height * 48) / DEF_HEIGHT) //mxd
#define CINE_BORDER_BOTTOM_HEIGHT	((viddef.height * 64) / DEF_HEIGHT) //mxd

static void SCR_DrawCinematicBorders(void) // H2
{
	if (cls.key_dest != key_console && cl.frame.playerstate.cinematicfreeze)
	{
		re.DrawFill(0, 0, viddef.width, CINE_BORDER_TOP_HEIGHT, TextPalette[P_BLACK]);
		re.DrawFill(0, viddef.height - CINE_BORDER_BOTTOM_HEIGHT, viddef.width, CINE_BORDER_BOTTOM_HEIGHT, TextPalette[P_BLACK]);
	}
}

static void SCR_DrawGameMessage(void) // H2
{
	display_msg.dispay_time -= cls.rframetime;

	if (display_msg.dispay_time <= 0.0f)
		return;

	//mxd. Calculate fade alpha based on elapsed time within the message display duration.
	float elapsed_time = display_msg.total_display_time - display_msg.dispay_time; //mxd. Time since message started.
	float fade_alpha = 1.0f;

	// Calculate fade-in effect (at the beginning).
	if (scr_text_fade_in->value > 0.0f && elapsed_time < scr_text_fade_in->value)
	{
		fade_alpha = elapsed_time / scr_text_fade_in->value; // Linear fade-in from 0 to 1
	}
	// Calculate fade-out effect (in the last portion of display time).
	else if (scr_text_fade_out->value > 0.0f && display_msg.dispay_time < scr_text_fade_out->value)
	{
		fade_alpha = display_msg.dispay_time / scr_text_fade_out->value; // Linear fade-out from 1 to 0
	}

	fade_alpha = Clamp(fade_alpha, 0.0f, 1.0f); //mxd. Ensure alpha is within valid range.

	//mxd. The above code was in a separate function in original version.

	//mxd. When drawing a caption, start at vertical center of bottom cinematic border. Original logic uses viddef.height * 0.9f instead
	// (can result in caption text drawn above cinematic border when drawing 4-line messages).
	int start_y;
	if (display_msg.is_caption)
		start_y = viddef.height - CINE_BORDER_BOTTOM_HEIGHT / 2;
	else
		start_y = (int)((float)viddef.height * 0.4f);

	int y = start_y - (display_msg.num_lines * ui_char_size / 2);

	const char* s = &display_msg.message[0];
	while (true)
	{
		int line_len;
		for (line_len = 0; line_len < MAX_MESSAGE_LINE_LENGTH; line_len++)
			if (s[line_len] == 0 || s[line_len] == '\n')
				break;

		//mxd. Ignore trailing spaces when centering...
		int trimmed_len = line_len;
		while (trimmed_len > 0 && s[trimmed_len - 1] == ' ')
			trimmed_len--;

		const int x = (viddef.width - trimmed_len * ui_char_size) / 2;
		SCR_AddDirtyPoint(x, y);
		DrawStringAlpha(x, y, s, display_msg.color, trimmed_len, fade_alpha); //mxd. Use DrawStringAlpha with fade effect.
		SCR_AddDirtyPoint(x + trimmed_len * ui_char_size, y + ui_char_size);

		// Skip to next line.
		s += line_len;

		if (*s == 0)
			break;

		s++; // Skip newline char.
		y += ui_char_size;
	}
}

static void SCR_UpdateFogDensity(void)
{
	static int old_time;
	static float old_density;

	if (cl.frame.playerstate.fog_density > 0.0f) //TODO: != 0.0f in original logic. Can playerstate.fog_density be negative?
	{
		r_fog_density->value += (cl.frame.playerstate.fog_density - old_density) * (float)(curtime - old_time) * 0.0008f; //mxd. Sys_Milliseconds() -> curtime.
		old_density = r_fog_density->value;
		r_fog->value = 1.0f;
	}
	else
	{
		r_fog_density->value = 0.0f;
		old_density = 0.0f;
		r_fog->value = 0.0f;
	}

	old_time = curtime;
}

static void SCR_DrawFramecounter(void) //mxd
{
	static char text_buf[32];

	if ((int)cl_frametime->value)
	{
		sprintf_s(text_buf, sizeof(text_buf), "FPS: %3.1f", (double)(1.0f / cls.rframetime));
		DrawString(viddef.width - (int)strlen(text_buf) * ui_char_size - 2 * ui_scale, 2 * ui_scale, text_buf, TextPalette[P_YELLOW], -1);
	}
}

//mxd. Draw fade-in effect during program initialization for smoother user experience.
static void SCR_DrawInitialFadeIn(void)
{
#define INITIAL_FADE_IN_DELAY	300

	static qboolean draw_fade = true;
	static int fade_end_time = INT_MAX;

	if (!draw_fade || cl.cinematictime > 0)
		return;

	// Disable fade when we are loading something, fade-in animation ended or Com_Error() was called.
	if (scr_progressbar_width > 0 || fade_end_time <= cls.realtime || Com_GetLastError() != 0)
	{
		draw_fade = false;
		return;
	}

	paletteRGBA_t color = TextPalette[P_BLACK];

	// Menu fade-in.
	if (cls.key_dest == key_menu)
	{
		if (fade_end_time == INT_MAX)
			fade_end_time = cls.realtime + INITIAL_FADE_IN_DELAY;

		const float lerp = 1.0f - (float)(fade_end_time - cls.realtime) / (float)INITIAL_FADE_IN_DELAY; // [0.0f .. 1.0f]
		color.a = (byte)((1.0f - sinf(lerp * ANGLE_90)) * 255.0f);
		color.a = max(1, color.a);
	}

	re.DrawFill(0, 0, viddef.width, viddef.height, color);
}

//mxd. Draw level transition fade effect (fade to/from black).
static void SCR_DrawLevelFade(void)
{
	if (level_fade_state == LEVEL_FADE_NONE)
		return;

	const int elapsed = cls.realtime - level_fade_start_time;

	// Check if fade is complete.
	if (elapsed >= level_fade_duration)
	{
		// Fade-out completed - hold at black (loading will continue).
		if (level_fade_state == LEVEL_FADE_OUT)
		{
			re.DrawFill(0, 0, viddef.width, viddef.height, TextPalette[P_BLACK]);
			return;
		}

		// Fade-in completed - clear fade state.
		level_fade_state = LEVEL_FADE_NONE;
		return;
	}

	// Calculate alpha based on fade direction.
	const float progress = (float)elapsed / (float)level_fade_duration; // [0.0f .. 1.0f]
	float alpha;

	if (level_fade_state == LEVEL_FADE_OUT)
		alpha = sinf(progress * ANGLE_90); // Fade from transparent to black.
	else // LEVEL_FADE_IN
		alpha = 1.0f - sinf(progress * ANGLE_90); // Fade from black to transparent.

	paletteRGBA_t color = TextPalette[P_BLACK];
	color.a = (byte)(Clamp(alpha, 0.0f, 1.0f) * 255.0f);

	if (color.a > 0)
		re.DrawFill(0, 0, viddef.width, viddef.height, color);
}

// Start a level fade-out (before loading) or fade-in (after loading).
void SCR_StartLevelFade(const qboolean fade_out)
{
	if (fade_out)
	{
		if (scr_level_fade_out->value <= 0.0f)
			return;

		level_fade_state = LEVEL_FADE_OUT;
		level_fade_duration = (int)(scr_level_fade_out->value * 1000.0f);
	}
	else
	{
		if (scr_level_fade_in->value <= 0.0f)
			return;

		level_fade_state = LEVEL_FADE_IN;
		level_fade_duration = (int)(scr_level_fade_in->value * 1000.0f);
	}

	level_fade_start_time = cls.realtime;
}

// Check if currently fading out.
qboolean SCR_IsFadingOut(void)
{
	return level_fade_state == LEVEL_FADE_OUT;
}

// Clear any active level fade.
void SCR_ClearLevelFade(void)
{
	level_fade_state = LEVEL_FADE_NONE;
}

//mxd. Draw "Interact" popup when player is near an interactable object.
static void SCR_DrawInteractIndicator(void)
{
	if (cl.frame.playerstate.stats[STAT_INTERACT] == 0)
		return;

	const char* text = "Interact";
	const int text_len = (int)strlen(text);
	const int x = (viddef.width - text_len * ui_char_size) / 2;
	const int y = viddef.height - 100 * ui_scale;

	SCR_AddDirtyPoint(x, y);
	SCR_AddDirtyPoint(x + text_len * ui_char_size, y + ui_char_size);
	DrawString(x, y, text, TextPalette[P_WHITE], -1);
}

// This is called every frame, and can also be called explicitly to flush text to the screen.
void SCR_UpdateScreen(void)
{
	int numframes;
	float separation[2] = { 0.0f, 0.0f };

	// If the screen is disabled (loading plaque is up, or vid mode changing) or screen/console aren't initialized, do nothing at all.
	if (cls.disable_screen || !scr_initialized || !con.initialized)
		return;

	// Range check cl_camera_separation so we don't inadvertently fry someone's brain.
	if (cl_stereo_separation->value > 1.0f)
		Cvar_SetValue("cl_stereo_separation", 1.0f);
	else if (cl_stereo_separation->value < 0.0f)
		Cvar_SetValue("cl_stereo_separation", 0.0f);

	if ((int)cl_stereo->value)
	{
		separation[0] = -cl_stereo_separation->value * 0.5f;
		separation[1] = cl_stereo_separation->value * 0.5f;
		numframes = 2;
	}
	else
	{
		separation[0] = 0.0f;
		separation[1] = 0.0f;
		numframes = 1;
	}

	SCR_UpdateFogDensity(); // H2

	for (int i = 0; i < numframes; i++)
	{
		re.BeginFrame(separation[i]);

		// If a cinematic is supposed to be running, handle menus and console specially.
		if (cl.cinematictime > 0)
		{
			SCR_DrawCinematic();

			if (cls.key_dest == key_menu)
				M_Draw();
			else if (cls.key_dest == key_console)
				SCR_DrawConsole();
		}
		else
		{
			// Do 3D refresh drawing, and then update the screen.
			SCR_CalcVrect();

			// Clear any dirty part of the background.
			SCR_TileClear();

			V_RenderView(separation[i]);

			SCR_DrawStats();
			SCR_DrawLayout();
			SCR_DrawNet();
			SCR_DrawNames(); // H2
			SCR_DrawInteractIndicator(); //mxd
			SCR_DrawCinematicBorders(); // H2
			SCR_DrawGameMessage(); // H2

			SCR_AddTimeGraph(); // H2
			SCR_DrawDebugGraph();
			SCR_DrawConsole();
			SCR_DrawPause();
			SCR_DrawLoading();

			M_Draw();
		}
	}

	SCR_DrawInitialFadeIn(); //mxd
	SCR_DrawLevelFade();     //mxd. Level transition fade effect.
	DBG_DrawMessages(); //mxd.
	SCR_DrawFramecounter(); //mxd

	re.EndFrame();
}
