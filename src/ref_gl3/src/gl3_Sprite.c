//
// gl3_Sprite.c -- sprite rendering
//
// OpenGL 3.3 Core Profile sprite rendering.
// Based on gl1_Sprite.c -- Copyright 1998 Raven Software
//

#include "gl3_Sprite.h"
#include "gl3_Image.h"
#include "gl3_Misc.h"
#include "gl3_Shaders.h"
#include "q_Sprite.h"
#include "Vector.h"

// Helper: write a 9-float vertex (pos3+tc2+col4).
static void WriteSpriteVert(float* dest, const vec3_t pos, float s, float t, float r, float g, float b, float a)
{
	dest[0] = pos[0]; dest[1] = pos[1]; dest[2] = pos[2];
	dest[3] = s; dest[4] = t;
	dest[5] = r; dest[6] = g; dest[7] = b; dest[8] = a;
}

// Standard square sprite.
static void R_DrawStandardSprite(const entity_t* e, const dsprframe_t* frame, const vec3_t up, const vec3_t right)
{
	vec3_t point;
	float quad[4 * 9];

	const float xl = (float)-frame->origin_x * e->scale;
	const float xr = (float)(frame->width - frame->origin_x) * e->scale;
	const float yt = (float)-frame->origin_y * e->scale;
	const float yb = (float)(frame->height - frame->origin_y) * e->scale;

	// Vert 0
	VectorMA(e->origin, yt, up, point);
	VectorMA(point, xl, right, point);
	WriteSpriteVert(&quad[0], point, 0.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f);

	// Vert 1
	VectorMA(e->origin, yb, up, point);
	VectorMA(point, xl, right, point);
	WriteSpriteVert(&quad[9], point, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f, 1.0f);

	// Vert 2
	VectorMA(e->origin, yb, up, point);
	VectorMA(point, xr, right, point);
	WriteSpriteVert(&quad[18], point, 1.0f, 0.0f, 1.0f, 1.0f, 1.0f, 1.0f);

	// Vert 3
	VectorMA(e->origin, yt, up, point);
	VectorMA(point, xr, right, point);
	WriteSpriteVert(&quad[27], point, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f);

	GL3_Draw3DPoly(GL_TRIANGLE_FAN, quad, 4);
}

// Sprite with 4 variable verts(x, y scale and s, t); texture must be square.
static void R_DrawDynamicSprite(const entity_t* e, const vec3_t up, const vec3_t right)
{
	vec3_t point;
	float quad[4 * 9];

	for (int i = 0; i < 4; i++)
	{
		VectorMA(e->origin, e->scale * e->verts[i].y, up, point);
		VectorMA(point, e->scale * e->verts[i].x, right, point);
		WriteSpriteVert(&quad[i * 9], point, e->verts[i].s, e->verts[i].t, 1.0f, 1.0f, 1.0f, 1.0f);
	}

	GL3_Draw3DPoly(GL_TRIANGLE_FAN, quad, 4);
}

// Sprite with n variable verts(x, y scale and s, t); texture must be square.
static void R_DrawVariableSprite(const entity_t* e, const vec3_t up, const vec3_t right)
{
	vec3_t point;
	float vbuf[64 * 9];

	svertex_t* v = &e->verts_p[0];
	for (int i = 0; i < e->numVerts && i < 64; i++, v++)
	{
		VectorMA(e->origin, e->scale * v->y, up, point);
		VectorMA(point, e->scale * v->x, right, point);
		WriteSpriteVert(&vbuf[i * 9], point, v->s, v->t, 1.0f, 1.0f, 1.0f, 1.0f);
	}

	GL3_Draw3DPoly(GL_TRIANGLE_FAN, vbuf, e->numVerts);
}

// Long linear semi-oriented sprite with two verts (xyz start and end) and a width.
static void R_DrawLineSprite(const entity_t* e, const vec3_t up)
{
	vec3_t diff;
	VectorSubtract(e->endpos, e->startpos, diff);

	vec3_t dir;
	CrossProduct(diff, up, dir);
	VectorNormalize(dir);

	vec3_t start_offset;
	VectorScale(dir, e->scale * 0.5f, start_offset);

	vec3_t end_offset;
	VectorScale(dir, e->scale2 * 0.5f, end_offset);

	const float tile = (e->tile > 0.0f ? e->tile : 1.0f);

	vec3_t point;
	float quad[4 * 9];

	VectorSubtract(e->startpos, start_offset, point);
	WriteSpriteVert(&quad[0], point, 0.0f, e->tileoffset, 1.0f, 1.0f, 1.0f, 1.0f);

	VectorAdd(e->startpos, start_offset, point);
	WriteSpriteVert(&quad[9], point, 1.0f, e->tileoffset, 1.0f, 1.0f, 1.0f, 1.0f);

	VectorAdd(e->endpos, end_offset, point);
	WriteSpriteVert(&quad[18], point, 1.0f, e->tileoffset + tile, 1.0f, 1.0f, 1.0f, 1.0f);

	VectorSubtract(e->endpos, end_offset, point);
	WriteSpriteVert(&quad[27], point, 0.0f, e->tileoffset + tile, 1.0f, 1.0f, 1.0f, 1.0f);

	GL3_Draw3DPoly(GL_TRIANGLE_FAN, quad, 4);
}

void R_DrawSpriteModel(entity_t* e)
{
	const model_t* mdl = *e->model;

	// Don't even bother culling, because it's just a single polygon without a surface cache.
	const dsprite_t* psprite = mdl->extradata;

	if (e->frame < 0 || e->frame >= psprite->numframes)
	{
		ri.Con_Printf(PRINT_DEVELOPER, "R_DrawSpriteModel: sprite '%s' with invalid frame %i!\n", mdl->name, e->frame);
		e->frame = 0;
	}

	e->frame %= psprite->numframes;

	if (mdl->skins[e->frame] == NULL)
		return;

	R_HandleTransparency(e);
	R_BindImage(mdl->skins[e->frame]);

	if (e->flags & RF_NODEPTHTEST)
		glDisable(GL_DEPTH_TEST);

	vec3_t up;
	vec3_t right;

	if (e->flags & RF_FIXED)
	{
		vec3_t fwd;
		DirAndUpFromAngles(e->angles, fwd, up);

		CrossProduct(up, fwd, right);
		VectorNormalize(right);
	}
	else
	{
		VectorCopy(vup, up);
		VectorCopy(vright, right);
	}

	switch (e->spriteType)
	{
		case SPRITE_EDICT:
		case SPRITE_STANDARD:
			R_DrawStandardSprite(e, &psprite->frames[e->frame], up, right);
			break;

		case SPRITE_DYNAMIC:
			R_DrawDynamicSprite(e, up, right);
			break;

		case SPRITE_VARIABLE:
			R_DrawVariableSprite(e, up, right);
			break;

		case SPRITE_LINE:
			R_DrawLineSprite(e, vpn);
			break;

		default:
			ri.Sys_Error(ERR_DROP, "R_DrawSpriteModel: unknown sprite type (%i)!", e->spriteType);
			break;
	}

	if (e->flags & RF_NODEPTHTEST)
		glEnable(GL_DEPTH_TEST);

	R_CleanupTransparency(e);
}
