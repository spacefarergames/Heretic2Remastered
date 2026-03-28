//
// gl3_Warp.c
//
// OpenGL 3.3 warped surfaces rendering.
// Based on gl1_Warp.c -- Copyright 1998 Raven Software
//

#include "gl3_Warp.h"
#include "gl3_Shaders.h"
#include "Hunk.h"
#include "turbsin.h"
#include "Vector.h"

#define SUBDIVIDE_SIZE	64.0f

#pragma region ========================== POLYGON GENERATION ==========================

// Build a 9-float (pos3+tc2+col4) vertex from glpoly vert + explicit s,t + pos_z override.
static void WriteWarpVert(float* out, const float* v, float s, float t, float pz)
{
	out[0] = v[0]; out[1] = v[1]; out[2] = pz;
	out[3] = s;    out[4] = t;
	out[5] = 1.0f; out[6] = 1.0f; out[7] = 1.0f; out[8] = 1.0f;
}

void R_EmitWaterPolys(const msurface_t* fa, const qboolean undulate, const qboolean use_reflect)
{
	float scroll;

	if (fa->texinfo->flags & SURF_FLOWING)
		scroll = -64.0f * ((r_newrefdef.time * 0.5f) - floorf(r_newrefdef.time * 0.5f));
	else
		scroll = 0.0f;

	for (const glpoly_t* p = fa->polys; p != NULL; p = p->next)
	{
		float vbuf[64 * 9];
		const float* v = p->verts[0];

		for (int i = 0; i < p->numverts; i++, v += VERTEXSIZE)
		{
			const float os = v[3];
			const float ot = v[4];

			float s = os + turbsin[(int)((ot * 0.125f + r_newrefdef.time) * TURBSCALE) & 255];
			s += scroll;
			s /= 64.0f;

			float t = ot + turbsin[(int)((os * 0.125f + r_newrefdef.time) * TURBSCALE) & 255];
			t /= 64.0f;

			float pz = v[2];
			if (undulate)
				pz += turbsin[TURBSIN_V0(v[0], v[1], r_newrefdef.time)] * 0.25f +
				      turbsin[TURBSIN_V1(v[0], v[1], r_newrefdef.time)] * 0.125f;

			WriteWarpVert(&vbuf[i * 9], v, s, t, pz);
		}

		if (use_reflect)
			GL3_DrawWaterPoly(GL_TRIANGLE_FAN, vbuf, p->numverts);
		else
			GL3_Draw3DPoly(GL_TRIANGLE_FAN, vbuf, p->numverts);
	}
}

void R_EmitUnderwaterPolys(const msurface_t* fa)
{
	for (const glpoly_t* p = fa->polys; p != NULL; p = p->next)
	{
		float vbuf[64 * 9];
		const float* v = p->verts[0];

		for (int i = 0; i < p->numverts; i++, v += VERTEXSIZE)
		{
			const float pz = v[2] +
				turbsin[TURBSIN_V0(v[0], v[1], r_newrefdef.time)] * 0.5f +
				turbsin[TURBSIN_V1(v[0], v[1], r_newrefdef.time)] * 0.25f;

			WriteWarpVert(&vbuf[i * 9], v, v[3], v[4], pz);
		}

		GL3_Draw3DPoly(GL_TRIANGLE_FAN, vbuf, p->numverts);
	}
}

void R_EmitQuakeFloorPolys(const msurface_t* fa)
{
	const float amount = quake_amount->value * 0.05f;

	for (const glpoly_t* p = fa->polys; p != NULL; p = p->next)
	{
		float vbuf[64 * 9];
		const float* v = p->verts[0];

		for (int i = 0; i < p->numverts; i++, v += VERTEXSIZE)
		{
			const float pz = v[2] +
				turbsin[TURBSIN_V0(v[0], v[1], r_newrefdef.time)] * amount * 0.5f +
				turbsin[TURBSIN_V1(v[0], v[1], r_newrefdef.time)] * amount * 0.25f;

			WriteWarpVert(&vbuf[i * 9], v, v[3], v[4], pz);
		}

		GL3_Draw3DPoly(GL_TRIANGLE_FAN, vbuf, p->numverts);
	}
}

#pragma endregion

static void R_BoundPoly(const int numverts, const float* verts, vec3_t mins, vec3_t maxs)
{
	ClearBounds(mins, maxs);

	const float* v = verts;
	for (int i = 0; i < numverts; i++)
		for (int j = 0; j < 3; j++, v++)
		{
			mins[j] = min(*v, mins[j]);
			maxs[j] = max(*v, maxs[j]);
		}
}

static void R_SubdividePolygon(msurface_t* warpface, const int numverts, float* verts)
{
	vec3_t mins, maxs;
	vec3_t front[64], back[64];
	float dist[64];
	vec3_t total;

	if (numverts > 60)
		ri.Sys_Error(ERR_DROP, "numverts = %i", numverts);

	R_BoundPoly(numverts, verts, mins, maxs);

	for (int i = 0; i < 3; i++)
	{
		float m = (mins[i] + maxs[i]) * 0.5f;
		m = SUBDIVIDE_SIZE * floorf(m / SUBDIVIDE_SIZE + 0.5f);

		if (maxs[i] - m < 8.0f || m - mins[i] < 8.0f)
			continue;

		float* v = verts + i;
		for (int j = 0; j < numverts; j++, v += 3)
			dist[j] = *v - m;

		dist[numverts] = dist[0];
		v -= i;
		VectorCopy(verts, v);

		int f = 0, b = 0;
		v = verts;
		for (int j = 0; j < numverts; j++, v += 3)
		{
			if (dist[j] >= 0) { VectorCopy(v, front[f]); f++; }
			if (dist[j] <= 0) { VectorCopy(v, back[b]);  b++; }

			if (dist[j] == 0.0f || dist[j + 1] == 0.0f)
				continue;

			if ((dist[j] > 0) != (dist[j + 1] > 0))
			{
				const float frac = dist[j] / (dist[j] - dist[j + 1]);
				for (int k = 0; k < 3; k++)
					front[f][k] = back[b][k] = v[k] + frac * (v[3 + k] - v[k]);
				f++; b++;
			}
		}

		R_SubdividePolygon(warpface, f, front[0]);
		R_SubdividePolygon(warpface, b, back[0]);
		return;
	}

	glpoly_t* poly = Hunk_Alloc((int)sizeof(glpoly_t) + ((numverts - 4) + 2) * VERTEXSIZE * sizeof(float));
	poly->next = warpface->polys;
	warpface->polys = poly;
	poly->numverts = numverts + 2;
	VectorClear(total);

	float total_s = 0.0f, total_t = 0.0f;

	for (int i = 0; i < numverts; i++, verts += 3)
	{
		VectorCopy(verts, poly->verts[i + 1]);
		const float s = DotProduct(verts, warpface->texinfo->vecs[0]);
		const float t = DotProduct(verts, warpface->texinfo->vecs[1]);

		total_s += s;
		total_t += t;
		Vec3AddAssign(verts, total);

		poly->verts[i + 1][3] = s;
		poly->verts[i + 1][4] = t;
	}

	VectorScale(total, 1.0f / (float)numverts, poly->verts[0]);
	poly->verts[0][3] = total_s / (float)numverts;
	poly->verts[0][4] = total_t / (float)numverts;

	memcpy(poly->verts[numverts + 1], poly->verts[1], sizeof(poly->verts[0]));
}

void R_SubdivideSurface(const model_t* mdl, msurface_t* fa)
{
	static vec3_t verts[64];
	float* vec;

	int numverts;
	for (numverts = 0; numverts < fa->numedges; numverts++)
	{
		const int lindex = mdl->surfedges[fa->firstedge + numverts];

		if (lindex > 0)
			vec = mdl->vertexes[mdl->edges[lindex].v[0]].position;
		else
			vec = mdl->vertexes[mdl->edges[-lindex].v[1]].position;

		VectorCopy(vec, verts[numverts]);
	}

	R_SubdividePolygon(fa, numverts, verts[0]);
}

#pragma endregion
