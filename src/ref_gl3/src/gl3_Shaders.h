//
// gl3_Shaders.h
//
// OpenGL 3.3 shader compilation and management.
//

#pragma once

#include "gl3_Local.h"

extern qboolean GL3_InitShaders(void);
extern void GL3_ShutdownShaders(void);
extern void GL3_UseShader(GLuint program);

// Matrix uniform helpers.
extern void GL3_UpdateProjection2D(float width, float height);
extern void GL3_UpdateProjection3D(float fov_y, float aspect, float znear, float zfar);
extern void GL3_UpdateModelview3D(const float* matrix4x4);
extern void GL3_UpdateModelviewLM(const float* matrix4x4);

// Per-draw color helpers.
extern void GL3_SetLMColor(float r, float g, float b, float a);
extern void GL3_Set3DColor(float r, float g, float b, float a);

// Dynamic polygon drawing helpers.
// GL3_DrawLMPoly: VERTEXSIZE=7 floats/vert (pos3+tc2+lmtc2), uses shader3DLightmap.
extern void GL3_DrawLMPoly(const float* verts, int numverts);
// GL3_Draw3DPoly: 9 floats/vert (pos3+tc2+col4), uses shader3D.
extern void GL3_Draw3DPoly(GLenum mode, const float* verts, int numverts);

// FBO management and HDR composite.
extern qboolean GL3_InitFBO(int width, int height);
extern void GL3_ShutdownFBO(void);
extern void GL3_CompositeHDR(int w, int h, float exposure, float bloom_strength, float ao_strength);

// Bloom post-process: bright-pass extract + separable Gaussian blur.
extern qboolean GL3_InitBloom(int width, int height);
extern void GL3_ShutdownBloom(void);
extern void GL3_RenderBloom(float threshold, float strength);

// SSAO post-process: hemisphere depth sampling + box blur.
extern qboolean GL3_InitSSAO(int width, int height);
extern void GL3_ShutdownSSAO(void);
extern void GL3_RenderSSAO(float radius, float bias);

// Per-frame dynamic light update (transforms dlights to view-space, sets shader3D uniforms).
extern void GL3_UpdateDlights(void);
