//
// gl3_Light.c
//
// OpenGL 3.3 lighting system.
// Based on gl1_Light.c -- Copyright 1998 Raven Software
//

#include "gl3_Light.h"
#include "gl3_Shaders.h"
#include "Vector.h"

#define DLIGHT_CUTOFF	64.0f

byte minlight[256];

static int r_dlightframecount;
static float s_blocklights[34 * 34 * 3];

static vec3_t pointcolor;
static vec3_t lightspot;

// ============================================================
// Minimal 4x4 column-major matrix helpers for bmodel transforms.
// ============================================================

typedef struct { float m[16]; } gl3mat4_t;

static void GM_Identity(gl3mat4_t* m)
{
	memset(m->m, 0, sizeof(m->m));
	m->m[0] = m->m[5] = m->m[10] = m->m[15] = 1.0f;
}

static void GM_Translate(gl3mat4_t* m, const vec3_t v)
{
	m->m[12] += m->m[0]*v[0] + m->m[4]*v[1] + m->m[8]*v[2];
	m->m[13] += m->m[1]*v[0] + m->m[5]*v[1] + m->m[9]*v[2];
	m->m[14] += m->m[2]*v[0] + m->m[6]*v[1] + m->m[10]*v[2];
	m->m[15] += m->m[3]*v[0] + m->m[7]*v[1] + m->m[11]*v[2];
}

// Rotate: angles_rad = [pitch_rad, yaw_rad, roll_rad] in Heretic2 conventions.
// Replicates: RotateX(angles[0]) * RotateY(angles[1]) * RotateZ(angles[2]).
static void GM_Rotate(gl3mat4_t* m, const vec3_t angles_rad)
{
	const float cx = cosf(angles_rad[0]), sx = sinf(angles_rad[0]);
	const float cy = cosf(angles_rad[1]), sy = sinf(angles_rad[1]);
	const float cz = cosf(angles_rad[2]), sz = sinf(angles_rad[2]);

	// Rotation matrix R = Rx * Ry * Rz (column-major)
	float r[16];
	r[0]  = cy*cz;           r[1]  = cy*sz;           r[2]  = -sy;     r[3]  = 0;
	r[4]  = sx*sy*cz-cx*sz;  r[5]  = sx*sy*sz+cx*cz;  r[6]  = sx*cy;   r[7]  = 0;
	r[8]  = cx*sy*cz+sx*sz;  r[9]  = cx*sy*sz-sx*cz;  r[10] = cx*cy;   r[11] = 0;
	r[12] = 0;                r[13] = 0;                r[14] = 0;       r[15] = 1;

	float tmp[16];
	memcpy(tmp, m->m, sizeof(tmp));

	for (int col = 0; col < 4; col++)
		for (int row = 0; row < 4; row++)
			m->m[col*4+row] = tmp[0*4+row]*r[col*4+0] + tmp[1*4+row]*r[col*4+1] +
			                  tmp[2*4+row]*r[col*4+2] + tmp[3*4+row]*r[col*4+3];
}

// Transform world-space point by inverse of matrix (for moving a world point into model space).
static void GM_VectorInverseTransform(const gl3mat4_t* m, vec3_t out, const vec3_t in)
{
	const float* mm = m->m;
	const float tx = in[0] - mm[12];
	const float ty = in[1] - mm[13];
	const float tz = in[2] - mm[14];

	// Inverse rotation = transpose of rotation part.
	out[0] = tx*mm[0] + ty*mm[1] + tz*mm[2];
	out[1] = tx*mm[4] + ty*mm[5] + tz*mm[6];
	out[2] = tx*mm[8] + ty*mm[9] + tz*mm[10];
}

// Transform model-space point to world space.
static void GM_VectorTransform(const gl3mat4_t* m, vec3_t out, const vec3_t in)
{
	const float* mm = m->m;
	out[0] = mm[0]*in[0] + mm[4]*in[1] + mm[8]*in[2]  + mm[12];
	out[1] = mm[1]*in[0] + mm[5]*in[1] + mm[9]*in[2]  + mm[13];
	out[2] = mm[2]*in[0] + mm[6]*in[1] + mm[10]*in[2] + mm[14];
}

typedef struct
{
	gl3mat4_t matrix;
	qboolean updated;
} BmodelTransform_t;

static BmodelTransform_t r_bmodel_transforms[MAX_ENTITIES];

#pragma region ========================== DYNAMIC LIGHTS RENDERING ==========================

static void R_RenderDlight(const dlight_t* light)
{
	const float rad = light->intensity * 0.35f;
	const float r = (float)light->color.r / 255.0f * 0.2f;
	const float g = (float)light->color.g / 255.0f * 0.2f;
	const float b = (float)light->color.b / 255.0f * 0.2f;

	// TRIANGLE_FAN: center vertex first, then 17 ring vertices.
#define DLIGHT_VERTS 18
	float verts[DLIGHT_VERTS * 9];
	float* v = &verts[0];

	// Center vertex.
	v[0] = light->origin[0] - vpn[0] * rad;
	v[1] = light->origin[1] - vpn[1] * rad;
	v[2] = light->origin[2] - vpn[2] * rad;
	v[3] = 0.0f; v[4] = 0.0f;
	v[5] = r; v[6] = g; v[7] = b; v[8] = 1.0f;

	for (int i = 16; i >= 0; i--)
	{
		const float a = (float)i / 16.0f * ANGLE_360;
		const float sin_a = sinf(a);
		const float cos_a = cosf(a);

		v = &verts[(17 - i) * 9];
		v[0] = light->origin[0] + vright[0] * cos_a * rad + vup[0] * sin_a * rad;
		v[1] = light->origin[1] + vright[1] * cos_a * rad + vup[1] * sin_a * rad;
		v[2] = light->origin[2] + vright[2] * cos_a * rad + vup[2] * sin_a * rad;
		v[3] = 0.0f; v[4] = 0.0f;
		v[5] = 0.0f; v[6] = 0.0f; v[7] = 0.0f; v[8] = 0.0f;
	}

	GL3_Draw3DPoly(GL_TRIANGLE_FAN, verts, DLIGHT_VERTS);
#undef DLIGHT_VERTS
}

void R_RenderDlights(void)
{
	if (!(int)gl_flashblend->value)
		return;

	r_dlightframecount = r_framecount + 1;

	glDepthMask(GL_FALSE);
	glEnable(GL_BLEND);
	glBlendFunc(GL_ONE, GL_ONE);

	const dlight_t* l = &r_newrefdef.dlights[0];
	for (int i = 0; i < r_newrefdef.num_dlights; i++, l++)
		R_RenderDlight(l);

	glDisable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	glDepthMask(GL_TRUE);
}

#pragma endregion

#pragma region ========================== DYNAMIC LIGHTS MANAGEMENT ==========================

void R_MarkLights(dlight_t* light, const int bit, const mnode_t* node)
{
	if (node->contents != -1)
		return;

	const cplane_t* splitplane = node->plane;
	const float dist = DotProduct(light->origin, splitplane->normal) - splitplane->dist;

	if (dist > light->intensity - DLIGHT_CUTOFF)
	{
		R_MarkLights(light, bit, node->children[0]);
		return;
	}

	if (dist < -light->intensity + DLIGHT_CUTOFF)
	{
		R_MarkLights(light, bit, node->children[1]);
		return;
	}

	msurface_t* surf = &r_worldmodel->surfaces[node->firstsurface];
	for (int i = 0; i < node->numsurfaces; i++, surf++)
	{
		if (surf->dlightframe != r_dlightframecount)
		{
			surf->dlightbits = 0;
			surf->dlightframe = r_dlightframecount;
		}

		surf->dlightbits |= bit;
	}

	R_MarkLights(light, bit, node->children[0]);
	R_MarkLights(light, bit, node->children[1]);
}

void R_PushDlights(void)
{
	if (!(int)gl_flashblend->value)
	{
		r_dlightframecount = r_framecount + 1;

		dlight_t* l = &r_newrefdef.dlights[0];
		for (int i = 0; i < r_newrefdef.num_dlights; i++, l++)
			R_MarkLights(l, 1 << i, r_worldmodel->nodes);
	}
}

static void R_SetPointColor(const msurface_t* surf, const int ds, const int dt, vec3_t color)
{
	VectorClear(color);

	int r00 = 0, g00 = 0, b00 = 0;
	int r01 = 0, g01 = 0, b01 = 0;
	int r10 = 0, g10 = 0, b10 = 0;
	int r11 = 0, g11 = 0, b11 = 0;

	const int dsfrac = ds & 15;
	const int dtfrac = dt & 15;
	const int light_smax = (surf->extents[0] >> 4) + 1;
	const int light_tmax = (surf->extents[1] >> 4) + 1;
	const int line3 = light_smax * 3;
	const byte* lightmap = surf->samples + ((dt >> 4) * light_smax + (ds >> 4)) * 3;

	for (int maps = 0; maps < MAXLIGHTMAPS && surf->styles[maps] != 255; maps++)
	{
		vec3_t scale;
		for (int c = 0; c < 3; c++)
			scale[c] = r_newrefdef.lightstyles[surf->styles[maps]].rgb[c];

		r00 += (int)((float)lightmap[0] * scale[0]);
		g00 += (int)((float)lightmap[1] * scale[1]);
		b00 += (int)((float)lightmap[2] * scale[2]);
		r01 += (int)((float)lightmap[3] * scale[0]);
		g01 += (int)((float)lightmap[4] * scale[1]);
		b01 += (int)((float)lightmap[5] * scale[2]);
		r10 += (int)((float)lightmap[line3 + 0] * scale[0]);
		g10 += (int)((float)lightmap[line3 + 1] * scale[1]);
		b10 += (int)((float)lightmap[line3 + 2] * scale[2]);
		r11 += (int)((float)lightmap[line3 + 3] * scale[0]);
		g11 += (int)((float)lightmap[line3 + 4] * scale[1]);
		b11 += (int)((float)lightmap[line3 + 5] * scale[2]);

		lightmap += light_smax * light_tmax * 3;
	}

	color[0] += (float)(((((((r11-r10)*dsfrac>>4)+r10)-(((r01-r00)*dsfrac>>4)+r00))*dtfrac)>>4)+(((r01-r00)*dsfrac>>4)+r00)) / 255.0f;
	color[1] += (float)(((((((g11-g10)*dsfrac>>4)+g10)-(((g01-g00)*dsfrac>>4)+g00))*dtfrac)>>4)+(((g01-g00)*dsfrac>>4)+g00)) / 255.0f;
	color[2] += (float)(((((((b11-b10)*dsfrac>>4)+b10)-(((b01-b00)*dsfrac>>4)+b00))*dtfrac)>>4)+(((b01-b00)*dsfrac>>4)+b00)) / 255.0f;

	Vec3ScaleAssign(gl_modulate->value, color);
}

void R_ResetBmodelTransforms(void)
{
	for (int i = 0; i < r_newrefdef.num_entities; i++)
		r_bmodel_transforms[i].updated = false;
}

static int R_RecursiveLightPoint(const mnode_t* node, const vec3_t start, const vec3_t end)
{
	if (node->contents != -1)
		return -1;

	const cplane_t* plane = node->plane;
	const float front = DotProduct(start, plane->normal) - plane->dist;
	const float back  = DotProduct(end,   plane->normal) - plane->dist;
	const int side = (front < 0.0f);

	if ((back < 0.0f) == side)
		return R_RecursiveLightPoint(node->children[side], start, end);

	const float frac = front / (front - back);
	vec3_t mid;
	VectorLerp(start, frac, end, mid);

	const int r = R_RecursiveLightPoint(node->children[side], start, mid);
	if (r >= 0)
		return r;

	if ((back < 0.0f) == side)
		return -1;

	VectorCopy(mid, lightspot);

	msurface_t* surf = &r_worldmodel->surfaces[node->firstsurface];
	for (int i = 0; i < node->numsurfaces; i++, surf++)
	{
		if (surf->samples == NULL)
			continue;

		if (surf->flags & (SURF_DRAWTURB | SURF_DRAWSKY | SURF_SKIPDRAW))
			continue;

		const mtexinfo_t* tex = surf->texinfo;
		const int s = (int)(DotProduct(mid, tex->vecs[0]) + tex->vecs[0][3]);
		const int t = (int)(DotProduct(mid, tex->vecs[1]) + tex->vecs[1][3]);

		if (s < surf->texturemins[0] || t < surf->texturemins[1])
			continue;

		const int ds = s - surf->texturemins[0];
		const int dt = t - surf->texturemins[1];

		if (ds > surf->extents[0] || dt > surf->extents[1])
			continue;

		R_SetPointColor(surf, ds, dt, pointcolor);
		return 1;
	}

	return R_RecursiveLightPoint(node->children[!side], mid, end);
}

void R_LightPoint(const vec3_t p, vec3_t color, const qboolean check_bmodels)
{
	if (r_worldmodel->lightdata == NULL)
	{
		VectorSet(color, 1.0f, 1.0f, 1.0f);
		return;
	}

	const vec3_t end = VEC3_INITA(p, 0.0f, 0.0f, -3072.0f);
	const int r = R_RecursiveLightPoint(r_worldmodel->nodes, p, end);

	float dist_z;
	if (r == -1)
	{
		VectorSet(color, 0.25f, 0.25f, 0.25f);
		dist_z = end[2];
	}
	else
	{
		VectorCopy(pointcolor, color);
		dist_z = lightspot[2];
	}

	if (check_bmodels)
	{
		for (int i = 0; i < r_newrefdef.num_entities; i++)
		{
			const entity_t* e = r_newrefdef.entities[i];

			if ((e->flags & RF_TRANSLUCENT) || e->model == NULL || *e->model == NULL || (*e->model)->type != mod_brush)
				continue;

			const model_t* mdl = *e->model;

			if (Vec3IsZero(e->origin))
			{
				if (p[0] < mdl->mins[0] || p[0] > mdl->maxs[0] ||
					p[1] < mdl->mins[1] || p[1] > mdl->maxs[1] ||
					p[2] < mdl->mins[2] || end[2] > mdl->maxs[2])
					continue;
			}
			else
			{
				if (p[0] < e->origin[0] - mdl->radius || p[0] > e->origin[0] + mdl->radius ||
					p[1] < e->origin[1] - mdl->radius || p[1] > e->origin[1] + mdl->radius ||
					p[2] < e->origin[2] - mdl->radius || end[2] > e->origin[2] + mdl->radius)
					continue;
			}

			BmodelTransform_t* bt = &r_bmodel_transforms[i];
			if (!bt->updated)
			{
				GM_Identity(&bt->matrix);
				GM_Translate(&bt->matrix, e->origin);
				GM_Rotate(&bt->matrix, e->angles);
				bt->updated = true;
			}

			vec3_t e_start, e_end;
			GM_VectorInverseTransform(&bt->matrix, e_start, p);
			GM_VectorInverseTransform(&bt->matrix, e_end,   end);

			if (R_RecursiveLightPoint(mdl->nodes + mdl->firstnode, e_start, e_end) == -1)
				continue;

			vec3_t cur_spot;
			GM_VectorTransform(&bt->matrix, cur_spot, lightspot);

			if (cur_spot[2] > dist_z)
			{
				VectorCopy(pointcolor, color);
				dist_z = cur_spot[2];
			}
		}
	}

	dlight_t* dl = &r_newrefdef.dlights[0];
	vec3_t dl_color = VEC3_ZERO;
	for (int lnum = 0; lnum < r_newrefdef.num_dlights; lnum++, dl++)
	{
		const float dist = VectorSeparation(p, dl->origin);
		const float add = (dl->intensity - dist) / 256.0f;

		if (add > 0.0f)
			for (int i = 0; i < 3; i++)
				dl_color[i] += (float)dl->color.c_array[i] / 255.0f * add;
	}

	Vec3ScaleAssign(gl_modulate->value, dl_color);
	Vec3AddAssign(dl_color, color);
}

#pragma endregion

static void R_AddDynamicLights(const msurface_t* surf)
{
	const int smax = (surf->extents[0] >> 4) + 1;
	const int tmax = (surf->extents[1] >> 4) + 1;
	const mtexinfo_t* tex = surf->texinfo;

	for (int lnum = 0; lnum < r_newrefdef.num_dlights; lnum++)
	{
		if (!(surf->dlightbits & (1 << lnum)))
			continue;

		const dlight_t* dl = &r_newrefdef.dlights[lnum];
		float fdist = DotProduct(dl->origin, surf->plane->normal) - surf->plane->dist;
		const float frad = dl->intensity - fabsf(fdist);

		float fminlight = DLIGHT_CUTOFF;
		if (frad < fminlight)
			continue;
		fminlight = frad - fminlight;

		vec3_t impact;
		for (int i = 0; i < 3; i++)
			impact[i] = dl->origin[i] - surf->plane->normal[i] * fdist;

		const int local_0 = (int)(DotProduct(impact, tex->vecs[0]) + tex->vecs[0][3] - (float)surf->texturemins[0]);
		const int local_1 = (int)(DotProduct(impact, tex->vecs[1]) + tex->vecs[1][3] - (float)surf->texturemins[1]);

		float* pfBL = &s_blocklights[0];
		int ftacc = 0;
		for (int t = 0; t < tmax; t++, ftacc += 16)
		{
			const int td = abs(local_1 - ftacc);
			int fsacc = 0;
			for (int s = 0; s < smax; s++, fsacc += 16, pfBL += 3)
			{
				const int sd = abs(local_0 - fsacc);
				if (sd > td) fdist = (float)(sd + (td >> 1));
				else         fdist = (float)(td + (sd >> 1));

				if (fdist < fminlight)
				{
					pfBL[0] += (frad - fdist) * ((float)dl->color.r * gl_modulate->value / 255.0f);
					pfBL[1] += (frad - fdist) * ((float)dl->color.g * gl_modulate->value / 255.0f);
					pfBL[2] += (frad - fdist) * ((float)dl->color.b * gl_modulate->value / 255.0f);
				}
			}
		}
	}
}

void R_SetCacheState(msurface_t* surf)
{
	for (int maps = 0; maps < MAXLIGHTMAPS && surf->styles[maps] != 255; maps++)
		surf->cached_light[maps] = r_newrefdef.lightstyles[surf->styles[maps]].white;
}

void R_InitMinlight(void)
{
	const float ml = Clamp(gl_minlight->value, 0.0f, 255.0f);
	gl_state.minlight_set = (ml != 0.0f);

	if (gl_state.minlight_set)
	{
		for (int i = 0; i < 256; i++)
		{
			const int inf = (int)((255.0f - ml) * (float)i / 255.0f + ml);
			minlight[i] = (byte)ClampI(inf, 0, 255);
		}
	}
	else
	{
		for (int i = 0; i < 256; i++)
			minlight[i] = (byte)i;
	}
}

void R_BuildLightMap(const msurface_t* surf, byte* dest, int stride)
{
	if (surf->texinfo->flags & SURF_FULLBRIGHT)
		ri.Sys_Error(ERR_DROP, "R_BuildLightMap called for non-lit surface");

	const int smax = (surf->extents[0] >> 4) + 1;
	const int tmax = (surf->extents[1] >> 4) + 1;
	const int size = smax * tmax;

	if (size > (int)sizeof(s_blocklights) >> 4)
		ri.Sys_Error(ERR_DROP, "Bad s_blocklights size");

	if (surf->samples == NULL)
	{
		for (int i = 0; i < size * 3; i++)
			s_blocklights[i] = 255.0f;
	}
	else
	{
		const byte* lightmap = surf->samples;
		memset(s_blocklights, 0, sizeof(s_blocklights[0]) * size * 3);

		for (int maps = 0; maps < MAXLIGHTMAPS && surf->styles[maps] != 255; maps++)
		{
			float scale[3];
			for (int i = 0; i < 3; i++)
				scale[i] = gl_modulate->value * r_newrefdef.lightstyles[surf->styles[maps]].rgb[i];

			float* bl = &s_blocklights[0];
			for (int i = 0; i < size; i++, bl += 3)
				for (int j = 0; j < 3; j++)
					bl[j] += (float)lightmap[i * 3 + j] * scale[j];

			lightmap += size * 3;
		}

		if (surf->dlightframe == r_framecount)
			R_AddDynamicLights(surf);
	}

	stride -= (smax << 2);
	const float* bl = &s_blocklights[0];

	for (int i = 0; i < tmax; i++, dest += stride)
	{
		for (int j = 0; j < smax; j++, bl += 3, dest += 4)
		{
			int r = (int)bl[0];
			int g = (int)bl[1];
			int b = (int)bl[2];

			r = max(r, 0);
			g = max(g, 0);
			b = max(b, 0);

			const int mx = max(r, max(g, b));
			if (mx > 255)
			{
				const float t = 255.0f / (float)mx;
				r = (int)((float)r * t);
				g = (int)((float)g * t);
				b = (int)((float)b * t);
			}

			if (gl_state.minlight_set)
			{
				r = minlight[r];
				g = minlight[g];
				b = minlight[b];
			}

			dest[0] = (byte)r;
			dest[1] = (byte)g;
			dest[2] = (byte)b;
			dest[3] = 255;
		}
	}
}
