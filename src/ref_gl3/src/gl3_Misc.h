//
// gl3_Misc.h
//
// OpenGL 3.3 utility functions.
//

#pragma once

#include "ref.h"

extern void R_ScreenShot_f(void);
extern void R_Strings_f(void);

extern void R_SetDefaultState(void);
extern void R_DrawNullModel(const entity_t* e);

extern void R_TransformVector(const vec3_t v, vec3_t out);
extern void R_RotateForEntity(const entity_t* e);
extern qboolean R_PointToScreen(const vec3_t pos, vec3_t screen_pos);
extern paletteRGBA_t R_ModulateRGBA(paletteRGBA_t a, paletteRGBA_t b);
extern paletteRGBA_t R_GetSpriteShadelight(const vec3_t origin, byte alpha);

extern void R_HandleTransparency(const entity_t* e);
extern void R_CleanupTransparency(const entity_t* e);

// Column-major 4x4 matrix helpers.
extern void R_Mat4x4_Identity(float m[16]);
extern void R_Mat4x4_Mul(float out[16], const float A[16], const float B[16]);
extern void R_Mat4x4_Rotate(float m[16], float angle_deg, float x, float y, float z);
extern void R_Mat4x4_Translate(float m[16], float tx, float ty, float tz);
