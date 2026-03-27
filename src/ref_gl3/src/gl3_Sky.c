//
// gl3_Sky.c
//
// OpenGL 3.3 Core Profile sky rendering.
// Based on gl1_Sky.c -- Copyright 1998 Raven Software
//

#include "gl3_Sky.h"
#include "gl3_Image.h"
#include "gl3_Shaders.h"
#include "Vector.h"

#include <math.h>

#define MAX_CLIP_VERTS	64
#define ON_EPSILON		0.1f // Point on plane side epsilon.

static float skyrotate;
static vec3_t skyaxis;
static image_t* sky_images[6];

static float skymins[2][6];
static float skymaxs[2][6];
static float sky_min;
static float sky_max;

// Q2 counterpart.
static void R_DrawSkyPolygon(const int nump, vec3_t vecs)
{
	// s = [0]/[2], t = [1]/[2]
	static const int vec_to_st[6][3] =
	{
		{ -2,  3,  1 },
		{  2,  3, -1 },

		{  1,  3,  2 },
		{ -1,  3, -2 },

		{ -2, -1,  3 },
		{ -2,  1, -3 }
	};

	// Decide which face it maps to.
	vec3_t v = VEC3_ZERO;

	float* vp = vecs;
	for (int i = 0; i < nump; i++, vp += 3)
		Vec3AddAssign(vp, v);

	vec3_t av;
	VectorAbs(v, av);

	int axis;
	if (av[0] > av[1] && av[0] > av[2])
		axis = (v[0] < 0 ? 1 : 0);
	else if (av[1] > av[2] && av[1] > av[0])
		axis = (v[1] < 0 ? 3 : 2);
	else
		axis = (v[2] < 0 ? 5 : 4);

	float dv;
	float s;
	float t;

	// Project new texture coords.
	for (int i = 0; i < nump; i++, vecs += 3)
	{
		int j = vec_to_st[axis][2];
		if (j > 0)
			dv = vecs[j - 1];
		else
			dv = -vecs[-j - 1];

		if (dv < 0.001f)
			continue; // Don't divide by zero.

		j = vec_to_st[axis][0];
		if (j < 0)
			s = -vecs[-j - 1] / dv;
		else
			s = vecs[j - 1] / dv;

		j = vec_to_st[axis][1];
		if (j < 0)
			t = -vecs[-j - 1] / dv;
		else
			t = vecs[j - 1] / dv;

		skymins[0][axis] = min(s, skymins[0][axis]);
		skymins[1][axis] = min(t, skymins[1][axis]);

		skymaxs[0][axis] = max(s, skymaxs[0][axis]);
		skymaxs[1][axis] = max(t, skymaxs[1][axis]);
	}
}

// Q2 counterpart
static void R_ClipSkyPolygon(const int nump, vec3_t vecs, const int stage)
{
	static const vec3_t skyclip[] =
	{
		{  1.0f,  1.0f, 0.0f },
		{  1.0f, -1.0f, 0.0f },
		{  0.0f, -1.0f, 1.0f },
		{  0.0f,  1.0f, 1.0f },
		{  1.0f,  0.0f, 1.0f },
		{ -1.0f,  0.0f, 1.0f }
	};

	if (nump > MAX_CLIP_VERTS - 2)
		ri.Sys_Error(ERR_DROP, "R_ClipSkyPolygon: MAX_CLIP_VERTS");

	if (stage == 6)
	{
		// Fully clipped, so draw it.
		R_DrawSkyPolygon(nump, vecs);
		return;
	}

	qboolean front = false;
	qboolean back = false;
	const float* norm = skyclip[stage];
	float dists[MAX_CLIP_VERTS];
	int sides[MAX_CLIP_VERTS];

	float* v = &vecs[0];
	for (int i = 0; i < nump; i++, v += 3)
	{
		const float d = DotProduct(v, norm);

		if (d > ON_EPSILON)
		{
			front = true;
			sides[i] = SIDE_FRONT;
		}
		else if (d < -ON_EPSILON)
		{
			back = true;
			sides[i] = SIDE_BACK;
		}
		else
		{
			sides[i] = SIDE_ON;
		}

		dists[i] = d;
	}

	if (!front || !back)
	{
		// Not clipped.
		R_ClipSkyPolygon(nump, vecs, stage + 1);
		return;
	}

	// Clip it.
	sides[nump] = sides[0];
	dists[nump] = dists[0];
	VectorCopy(vecs, &vecs[nump * 3]);

	vec3_t newv[2][MAX_CLIP_VERTS];
	int newc[2] = { 0 };

	v = &vecs[0];
	for (int i = 0; i < nump; i++, v += 3)
	{
		switch (sides[i])
		{
			case SIDE_FRONT:
				VectorCopy(v, newv[0][newc[0]]);
				newc[0]++;
				break;

			case SIDE_BACK:
				VectorCopy(v, newv[1][newc[1]]);
				newc[1]++;
				break;

			case SIDE_ON:
				VectorCopy(v, newv[0][newc[0]]);
				newc[0]++;
				VectorCopy(v, newv[1][newc[1]]);
				newc[1]++;
				break;
		}

		if (sides[i] == SIDE_ON || sides[i + 1] == SIDE_ON || sides[i + 1] == sides[i])
			continue;

		const float d = dists[i] / (dists[i] - dists[i + 1]);
		for (int j = 0; j < 3; j++)
		{
			const float e = v[j] + d * (v[j + 3] - v[j]);
			newv[0][newc[0]][j] = e;
			newv[1][newc[1]][j] = e;
		}

		newc[0]++;
		newc[1]++;
	}

	// Continue.
	R_ClipSkyPolygon(newc[0], newv[0][0], stage + 1);
	R_ClipSkyPolygon(newc[1], newv[1][0], stage + 1);
}

// Q2 counterpart
void R_AddSkySurface(const msurface_t* fa)
{
	vec3_t verts[MAX_CLIP_VERTS];

	// Calculate vertex values for sky box.
	for (const glpoly_t* p = fa->polys; p != NULL; p = p->next)
	{
		for (int i = 0; i < p->numverts; i++)
			VectorSubtract(p->verts[i], r_origin, verts[i]);

		R_ClipSkyPolygon(p->numverts, verts[0], 0);
	}
}

// Q2 counterpart
void R_ClearSkyBox(void)
{
	for (int i = 0; i < 6; i++)
	{
		skymins[0][i] = 9999.0f;
		skymins[1][i] = 9999.0f;
		skymaxs[0][i] = -9999.0f;
		skymaxs[1][i] = -9999.0f;
	}
}

// GL3: compute sky vertex position and UV, store into output arrays.
static void R_MakeSkyVec(float s, float t, const int axis, vec3_t out_pos, float* out_s, float* out_t)
{
	// 1 = s, 2 = t, 3 = 2048
	static const int st_to_vec[6][3] =
	{
		{  3, -1,  2 },
		{ -3,  1,  2 },

		{  1,  3,  2 },
		{ -1, -3,  2 },

		{ -2, -1,  3 },	// 0 degrees yaw, look straight up.
		{  2, -1, -3 }	// Look straight down.
	};

	float clipdist;

	if ((int)r_fog->value)
		clipdist = r_farclipdist->value;
	else
		clipdist = r_farclipdist->value * 0.5773503f;

	vec3_t b;
	VectorSet(b, s * clipdist, t * clipdist, clipdist);

	for (int i = 0; i < 3; i++)
	{
		const int k = st_to_vec[axis][i];
		if (k < 0)
			out_pos[i] = -b[-k - 1];
		else
			out_pos[i] = b[k - 1];
	}

	// Avoid bilerp seam.
	s = (s + 1.0f) * 0.5f;
	t = (t + 1.0f) * 0.5f;

	s = Clamp(s, sky_min, sky_max);
	t = Clamp(t, sky_min, sky_max);

	*out_s = s;
	*out_t = 1.0f - t;
}

void R_DrawSkyBox(void)
{
	static const int skytexorder[] = { 0, 2, 1, 3, 4, 5 };

	if (skyrotate != 0.0f)
	{
		// Check for no sky at all.
		int side;
		for (side = 0; side < 6; side++)
			if (skymins[0][side] < skymaxs[0][side] && skymins[1][side] < skymaxs[1][side])
				break;

		if (side == 6)
			return; // Nothing visible.
	}

	// GL3: Build sky translation + rotation matrix and upload as modelview.
	// Equivalent of glPushMatrix() + glTranslatef(r_origin) + glRotatef(skyrotate...).
	{
		// Start from the current world matrix.
		float sky_mv[16];
		memcpy(sky_mv, r_world_matrix, sizeof(sky_mv));

		// Apply translation by r_origin (column-major: M = M * T(origin)).
		sky_mv[12] += sky_mv[0] * r_origin[0] + sky_mv[4] * r_origin[1] + sky_mv[8]  * r_origin[2];
		sky_mv[13] += sky_mv[1] * r_origin[0] + sky_mv[5] * r_origin[1] + sky_mv[9]  * r_origin[2];
		sky_mv[14] += sky_mv[2] * r_origin[0] + sky_mv[6] * r_origin[1] + sky_mv[10] * r_origin[2];
		sky_mv[15] += sky_mv[3] * r_origin[0] + sky_mv[7] * r_origin[1] + sky_mv[11] * r_origin[2];

		// Apply sky rotation if needed.
		if (skyrotate != 0.0f)
		{
			const float angle_rad = r_newrefdef.time * skyrotate * ((float)M_PI / 180.0f);
			const float c = cosf(angle_rad);
			const float s = sinf(angle_rad);

			// Normalize axis.
			float ax = skyaxis[0], ay = skyaxis[1], az = skyaxis[2];
			const float len = sqrtf(ax * ax + ay * ay + az * az);
			if (len > 0.0f) { ax /= len; ay /= len; az /= len; }

			// Build rotation matrix R (column-major).
			const float omc = 1.0f - c;
			float R[16];
			R[0]  = c + ax * ax * omc;         R[4]  = ax * ay * omc - az * s;     R[8]  = ax * az * omc + ay * s;     R[12] = 0.0f;
			R[1]  = ay * ax * omc + az * s;     R[5]  = c + ay * ay * omc;          R[9]  = ay * az * omc - ax * s;     R[13] = 0.0f;
			R[2]  = az * ax * omc - ay * s;     R[6]  = az * ay * omc + ax * s;     R[10] = c + az * az * omc;          R[14] = 0.0f;
			R[3]  = 0.0f;                       R[7]  = 0.0f;                       R[11] = 0.0f;                       R[15] = 1.0f;

			// Multiply: sky_mv = sky_mv * R
			float tmp[16];
			for (int col = 0; col < 4; col++)
			{
				for (int row = 0; row < 4; row++)
				{
					tmp[col * 4 + row] = sky_mv[0 * 4 + row] * R[col * 4 + 0]
										+ sky_mv[1 * 4 + row] * R[col * 4 + 1]
										+ sky_mv[2 * 4 + row] * R[col * 4 + 2]
										+ sky_mv[3 * 4 + row] * R[col * 4 + 3];
				}
			}

			memcpy(sky_mv, tmp, sizeof(sky_mv));
		}

		GL3_UpdateModelview3D(sky_mv);
	}

	for (int i = 0; i < 6; i++)
	{
		if (skyrotate != 0.0f)
		{
			// Hack, forces full sky to draw when rotating.
			skymins[0][i] = -1.0f;
			skymins[1][i] = -1.0f;
			skymaxs[0][i] = 1.0f;
			skymaxs[1][i] = 1.0f;
		}

		if (skymins[0][i] < skymaxs[0][i] && skymins[1][i] < skymaxs[1][i])
		{
			R_BindImage(sky_images[skytexorder[i]]);

			// GL3: Build 4 verts (9 floats each: pos3+tc2+col4) for a quad.
			float quad[4 * 9];
			vec3_t pos;
			float s_coord, t_coord;

			// Vert 0: skymins[0], skymins[1]
			R_MakeSkyVec(skymins[0][i], skymins[1][i], i, pos, &s_coord, &t_coord);
			quad[0] = pos[0]; quad[1] = pos[1]; quad[2] = pos[2];
			quad[3] = s_coord; quad[4] = t_coord;
			quad[5] = 1.0f; quad[6] = 1.0f; quad[7] = 1.0f; quad[8] = 1.0f;

			// Vert 1: skymins[0], skymaxs[1]
			R_MakeSkyVec(skymins[0][i], skymaxs[1][i], i, pos, &s_coord, &t_coord);
			quad[9]  = pos[0]; quad[10] = pos[1]; quad[11] = pos[2];
			quad[12] = s_coord; quad[13] = t_coord;
			quad[14] = 1.0f; quad[15] = 1.0f; quad[16] = 1.0f; quad[17] = 1.0f;

			// Vert 2: skymaxs[0], skymaxs[1]
			R_MakeSkyVec(skymaxs[0][i], skymaxs[1][i], i, pos, &s_coord, &t_coord);
			quad[18] = pos[0]; quad[19] = pos[1]; quad[20] = pos[2];
			quad[21] = s_coord; quad[22] = t_coord;
			quad[23] = 1.0f; quad[24] = 1.0f; quad[25] = 1.0f; quad[26] = 1.0f;

			// Vert 3: skymaxs[0], skymins[1]
			R_MakeSkyVec(skymaxs[0][i], skymins[1][i], i, pos, &s_coord, &t_coord);
			quad[27] = pos[0]; quad[28] = pos[1]; quad[29] = pos[2];
			quad[30] = s_coord; quad[31] = t_coord;
			quad[32] = 1.0f; quad[33] = 1.0f; quad[34] = 1.0f; quad[35] = 1.0f;

			GL3_Draw3DPoly(GL_TRIANGLE_FAN, quad, 4);
		}
	}

	// GL3: Restore world modelview matrix (equivalent of glPopMatrix).
	GL3_UpdateModelview3D(r_world_matrix);
}

void RI_SetSky(const char* name, const float rotate, const vec3_t axis)
{
	static const char* surf[] = { "rt", "bk", "lf", "ft", "up", "dn" };

	skyrotate = rotate;
	VectorCopy(axis, skyaxis);

	for (int i = 0; i < 6; i++)
	{
		sky_images[i] = R_FindImage(va("pics/skies/%s%s.m8", name, surf[i]), it_sky);

		if (skyrotate != 0.0f)
		{
			// Take less memory.
			sky_min = 1.0f / 256.0f;
			sky_max = 255.0f / 256.0f;
		}
		else
		{
			sky_min = 1.0f / 512.0f;
			sky_max = 511.0f / 512.0f;
		}
	}
}
