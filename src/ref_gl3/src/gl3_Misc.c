//
// gl3_Misc.c
//
// OpenGL 3.3 utility functions.
//

#include "gl3_Misc.h"
#include "gl3_Image.h"
#include "gl3_Light.h"
#include "gl3_Shaders.h"
#include "gl3_Local.h"
#include "Vector.h"
#include "vid.h"
#include <math.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

void R_ScreenShot_f(void)
{
#define SCREENSHOT_COMP	3

	const int buf_size = viddef.width * viddef.height * SCREENSHOT_COMP;
	byte* buffer = malloc(buf_size);

	if (buffer == NULL)
	{
		ri.Con_Printf(PRINT_ALL, "R_ScreenShot_f: couldn't malloc %i bytes!\n", buf_size);
		return;
	}

	glPixelStorei(GL_PACK_ALIGNMENT, 1);
	glReadPixels(0, 0, viddef.width, viddef.height, GL_RGB, GL_UNSIGNED_BYTE, buffer);

	ri.Vid_WriteScreenshot(viddef.width, viddef.height, SCREENSHOT_COMP, buffer);
	free(buffer);
}

void R_Strings_f(void)
{
	ri.Con_Printf(PRINT_ALL, "GL_VENDOR: %s\n", gl_config.vendor_string);
	ri.Con_Printf(PRINT_ALL, "GL_RENDERER: %s\n", gl_config.renderer_string);
	ri.Con_Printf(PRINT_ALL, "GL_VERSION: %s\n", gl_config.version_string);
}

void R_SetDefaultState(void)
{
	glClearColor(1.0f, 0.0f, 0.5f, 0.5f);
	glCullFace(GL_FRONT);

	glDisable(GL_DEPTH_TEST);
	glDisable(GL_CULL_FACE);
	glDisable(GL_BLEND);

	glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);

	R_TextureMode(gl_texturemode->string);

	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
}

void R_Mat4x4_Identity(float m[16])
{
	memset(m, 0, 16 * sizeof(float));
	m[0] = m[5] = m[10] = m[15] = 1.0f;
}

void R_Mat4x4_Mul(float out[16], const float A[16], const float B[16])
{
	// Column-major: out = A * B
	for (int c = 0; c < 4; c++)
		for (int r = 0; r < 4; r++)
		{
			float v = 0.0f;
			for (int k = 0; k < 4; k++)
				v += A[k * 4 + r] * B[c * 4 + k];
			out[c * 4 + r] = v;
		}
}

void R_Mat4x4_Rotate(float m[16], const float angle_deg, const float x, const float y, const float z)
{
	const float a = angle_deg * ((float)M_PI / 180.0f);
	const float c = cosf(a);
	const float s = sinf(a);

	float r[16];
	R_Mat4x4_Identity(r);

	if (x == 1.0f && y == 0.0f && z == 0.0f)
	{
		r[5] = c;  r[9] = -s;
		r[6] = s;  r[10] = c;
	}
	else if (x == 0.0f && y == 1.0f && z == 0.0f)
	{
		r[0] = c;  r[8] = s;
		r[2] = -s; r[10] = c;
	}
	else if (x == 0.0f && y == 0.0f && z == 1.0f)
	{
		r[0] = c;  r[4] = -s;
		r[1] = s;  r[5] = c;
	}

	float tmp[16];
	R_Mat4x4_Mul(tmp, m, r);
	memcpy(m, tmp, 16 * sizeof(float));
}

void R_Mat4x4_Translate(float m[16], const float tx, const float ty, const float tz)
{
	// m = m * T(tx, ty, tz)
	m[12] += m[0] * tx + m[4] * ty + m[8]  * tz;
	m[13] += m[1] * tx + m[5] * ty + m[9]  * tz;
	m[14] += m[2] * tx + m[6] * ty + m[10] * tz;
	m[15] += m[3] * tx + m[7] * ty + m[11] * tz;
}

void R_DrawNullModel(const entity_t* e)
{
	vec3_t shadelight;

	if (e->flags & RF_FULLBRIGHT)
		VectorSet(shadelight, 1.0f, 1.0f, 1.0f);
	else
		R_LightPoint(e->origin, shadelight, false);

	R_RotateForEntity(e);

	const float rc = shadelight[0], gc = shadelight[1], bc = shadelight[2];
	float vbuf[6 * 9];

	// Bottom fan: apex at -16Z, ring at Z=0
	float* v = &vbuf[0 * 9];
	v[0]=0.0f; v[1]=0.0f; v[2]=-16.0f; v[3]=0.0f; v[4]=0.0f; v[5]=rc; v[6]=gc; v[7]=bc; v[8]=1.0f;
	for (int i = 0; i < 5; i++)
	{
		v = &vbuf[(1 + i) * 9];
		v[0] = 16.0f * cosf((float)i * ANGLE_90);
		v[1] = 16.0f * sinf((float)i * ANGLE_90);
		v[2]=0.0f; v[3]=0.0f; v[4]=0.0f; v[5]=rc; v[6]=gc; v[7]=bc; v[8]=1.0f;
	}
	GL3_Draw3DPoly(GL_TRIANGLE_FAN, vbuf, 6);

	// Top fan: apex at +16Z, ring at Z=0, reversed winding
	v = &vbuf[0 * 9];
	v[0]=0.0f; v[1]=0.0f; v[2]=16.0f; v[3]=0.0f; v[4]=0.0f; v[5]=rc; v[6]=gc; v[7]=bc; v[8]=1.0f;
	for (int i = 4; i >= 0; i--)
	{
		v = &vbuf[(4 - i + 1) * 9];
		v[0] = 16.0f * cosf((float)i * ANGLE_90);
		v[1] = 16.0f * sinf((float)i * ANGLE_90);
		v[2]=0.0f; v[3]=0.0f; v[4]=0.0f; v[5]=rc; v[6]=gc; v[7]=bc; v[8]=1.0f;
	}
	GL3_Draw3DPoly(GL_TRIANGLE_FAN, vbuf, 6);

	GL3_UpdateModelview3D(r_world_matrix);
}

void R_TransformVector(const vec3_t v, vec3_t out)
{
	out[0] = DotProduct(v, vright);
	out[1] = DotProduct(v, vup);
	out[2] = DotProduct(v, vpn);
}

void R_RotateForEntity(const entity_t* e)
{
	// Build entity TRS matrix replicating GL1:
	// glTranslatef(origin) * glRotatef(yaw*RAD_TO_ANGLE, Z) * glRotatef(-pitch*RAD_TO_ANGLE, Y) * glRotatef(-roll*RAD_TO_ANGLE, X)
	float m[16];
	R_Mat4x4_Identity(m);
	R_Mat4x4_Translate(m, e->origin[0], e->origin[1], e->origin[2]);
	R_Mat4x4_Rotate(m,  e->angles[1] * RAD_TO_ANGLE, 0.0f, 0.0f, 1.0f);
	R_Mat4x4_Rotate(m, -e->angles[0] * RAD_TO_ANGLE, 0.0f, 1.0f, 0.0f);
	R_Mat4x4_Rotate(m, -e->angles[2] * RAD_TO_ANGLE, 1.0f, 0.0f, 0.0f);

	float entity_mv[16];
	R_Mat4x4_Mul(entity_mv, r_world_matrix, m);
	GL3_UpdateModelview3D(entity_mv);
}

qboolean R_PointToScreen(const vec3_t pos, vec3_t screen_pos)
{
	float tmp[8];

	// Modelview transform.
	tmp[0] = r_world_matrix[0] * pos[0] + r_world_matrix[4] * pos[1] + r_world_matrix[8] *  pos[2] + r_world_matrix[12];
	tmp[1] = r_world_matrix[1] * pos[0] + r_world_matrix[5] * pos[1] + r_world_matrix[9] *  pos[2] + r_world_matrix[13];
	tmp[2] = r_world_matrix[2] * pos[0] + r_world_matrix[6] * pos[1] + r_world_matrix[10] * pos[2] + r_world_matrix[14];
	tmp[3] = r_world_matrix[3] * pos[0] + r_world_matrix[7] * pos[1] + r_world_matrix[11] * pos[2] + r_world_matrix[15];

	// Projection transform.
	tmp[4] = r_projection_matrix[0] * tmp[0] + r_projection_matrix[4] * tmp[1] + r_projection_matrix[8] *  tmp[2] + r_projection_matrix[12] * tmp[3];
	tmp[5] = r_projection_matrix[1] * tmp[0] + r_projection_matrix[5] * tmp[1] + r_projection_matrix[9] *  tmp[2] + r_projection_matrix[13] * tmp[3];
	tmp[6] = r_projection_matrix[2] * tmp[0] + r_projection_matrix[6] * tmp[1] + r_projection_matrix[10] * tmp[2] + r_projection_matrix[14] * tmp[3];

	if (tmp[2] == 0.0f)
		return false;

	tmp[7] = 1.0f / -tmp[2];

	tmp[4] *= tmp[7];
	tmp[5] *= tmp[7];
	tmp[6] *= tmp[7];

	screen_pos[0] = (tmp[4] * 0.5f + 0.5f) * (float)r_newrefdef.width +  (float)r_newrefdef.x;
	screen_pos[1] = (tmp[5] * 0.5f + 0.5f) * (float)r_newrefdef.height + (float)r_newrefdef.y;
	screen_pos[2] = (1.0f + tmp[6]) * 0.5f;

	screen_pos[1] = (float)r_newrefdef.height - screen_pos[1];

	return true;
}

paletteRGBA_t R_ModulateRGBA(const paletteRGBA_t a, const paletteRGBA_t b)
{
	const paletteRGBA_t c = { .r = a.r * b.r / 255, .g = a.g * b.g / 255, .b = a.b * b.b / 255, .a = a.a * b.a / 255 };
	return c;
}

paletteRGBA_t R_GetSpriteShadelight(const vec3_t origin, const byte alpha)
{
	static const vec3_t light_add = { 0.1f, 0.1f, 0.1f };

	vec3_t c;
	R_LightPoint(origin, c, false);
	Vec3AddAssign(light_add, c);
	Vec3ScaleAssign(255.0f, c);

	const float max_val = max(c[0], max(c[1], c[2]));
	if (max_val > 255.0f)
		Vec3ScaleAssign(255.0f / max_val, c);

	const paletteRGBA_t color = { .r = (byte)c[0], .g = (byte)c[1], .b = (byte)c[2], alpha };
	return color;
}

void R_HandleTransparency(const entity_t* e)
{
	if (e->flags & RF_TRANS_ADD)
	{
		if (e->flags & RF_ALPHA_TEXTURE)
		{
			glBlendFunc(GL_SRC_ALPHA, GL_ONE);
		}
		else
		{
			glBlendFunc(GL_ONE, GL_ONE);
		}
	}
	else
	{
		glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	}

	glEnable(GL_BLEND);
}

void R_CleanupTransparency(const entity_t* e)
{
	glDisable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
}
