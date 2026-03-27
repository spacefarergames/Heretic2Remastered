//
// gl3_Light.h
//
// OpenGL 3.3 lighting system.
// Based on gl1_Light.h -- Copyright 1998 Raven Software
//

#pragma once

#include "gl3_Local.h"

extern byte minlight[256];

extern void R_RenderDlights(void);
extern void R_MarkLights(dlight_t* light, int bit, const mnode_t* node);
extern void R_PushDlights(void);
extern void R_ResetBmodelTransforms(void);
extern void R_LightPoint(const vec3_t p, vec3_t color, qboolean check_bmodels);

extern void R_SetCacheState(msurface_t* surf);
extern void R_InitMinlight(void);
extern void R_BuildLightMap(const msurface_t* surf, byte* dest, int stride);
