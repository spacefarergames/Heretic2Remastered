//
// gl3_Surface.h
//
// OpenGL 3.3 world/brush surface rendering.
// Based on gl1_Surface.h -- Copyright 1998 Raven Software
//

#pragma once

#include "gl3_Local.h"

extern int c_visible_lightmaps;
extern int c_visible_textures;

extern void R_DrawWorld(void);
extern void R_DrawBrushModel(entity_t* ent);
extern void R_MarkLeaves(void);
extern void R_SortAndDrawAlphaSurfaces(void);

extern void R_SetReflectionPass(qboolean active);
extern msurface_t* R_GetAlphaSurfaces(void);
extern void R_SetAlphaSurfaces(msurface_t* surf);
extern qboolean R_GetLastWaterPlaneZ(float* out_z);
