//
// gl3_Draw.h
//
// OpenGL 3.3 2D drawing system.
//

#pragma once

#include "gl3_Local.h"

// H2. Font character definition struct.
typedef struct glxy_s
{
	float xl;
	float yt;
	float xr;
	float yb;
	int w;
	int h;
	int baseline;
} glxy_t;

extern glxy_t* font1;
extern glxy_t* font2;

extern image_t* draw_chars;

extern void ShutdownFonts(void);

extern void Draw_InitLocal(void);

extern image_t* Draw_FindPic(const char* name);
extern void Draw_GetPicSize(int* w, int* h, const char* name);

extern void Draw_Pic(int x, int y, int scale, const char* name, float alpha);
extern void Draw_StretchPic(int x, int y, int w, int h, const char* name, float alpha, DrawStretchPicScaleMode_t mode);
extern void Draw_TileClear(int x, int y, int w, int h, const char* pic);
extern void Draw_Fill(int x, int y, int w, int h, paletteRGBA_t color);
extern void Draw_FadeScreen(paletteRGBA_t color);
extern void Draw_Name(const vec3_t origin, const char* name, paletteRGBA_t color);
extern void Draw_Char(int x, int y, int scale, int c, paletteRGBA_t color, qboolean draw_shadow);
extern void Draw_Render(int x, int y, int w, int h, const image_t* image, float alpha);

extern void Draw_BigFont(int x, int y, const char* text, float alpha);
extern int BF_Strlen(const char* text);
extern void Draw_BookPic(const char* name, float scale, float alpha);

extern void Draw_InitCinematic(int width, int height);
extern void Draw_CloseCinematic(void);
extern void Draw_Cinematic(const byte* data, const paletteRGB_t* palette);
