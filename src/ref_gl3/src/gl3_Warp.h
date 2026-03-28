//
// gl3_Warp.h
//
// OpenGL 3.3 warped surfaces.
// Based on gl1_Warp.h -- Copyright 1998 Raven Software
//

#pragma once

#include "gl3_Local.h"

extern void R_EmitWaterPolys(const msurface_t* fa, qboolean undulate, qboolean use_reflect);
extern void R_EmitUnderwaterPolys(const msurface_t* fa);
extern void R_EmitQuakeFloorPolys(const msurface_t* fa);
extern void R_SubdivideSurface(const model_t* mdl, msurface_t* fa);
