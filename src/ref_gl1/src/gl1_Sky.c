//
// gl1_Sky.c
//
// Copyright 1998 Raven Software
//

#include "gl1_Sky.h"
#include "gl1_Image.h"
#include "Vector.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#define MAX_CLIP_VERTS	64
#define ON_EPSILON		0.1f // Point on plane side epsilon.

#define SKY_CLOUD_GRID		8
#define SKY_CLOUD_ALPHA		0.35f
#define SKY_CLOUD_SPEED		0.02f
#define SKY_STAR_COUNT		400

static float skyrotate;
static vec3_t skyaxis;
static image_t* sky_images[6];

static float skymins[2][6];
static float skymaxs[2][6];
static float sky_min;
static float sky_max;

static float R_GetSkyClipDist(void)
{
	if ((int)r_fog->value)
		return r_farclipdist->value;

	return r_farclipdist->value * 0.5773503f;
}

static void R_SkyVec(float s, float t, const int axis, vec3_t out_pos, float* out_s, float* out_t)
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

	const float clipdist = R_GetSkyClipDist();

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

static float R_SkyNoise(const vec3_t dir, const float time, const float scale)
{
	const float n0 = sinf(dir[0] * (2.3f * scale) + time * (0.9f * scale));
	const float n1 = sinf(dir[1] * (3.1f * scale) - time * (0.7f * scale));
	const float n2 = sinf(dir[2] * (2.7f * scale) + time * (0.6f * scale));
	return (n0 + n1 + n2) * (1.0f / 3.0f);
}

static float R_SkyCloudAlpha(const vec3_t dir, const float time)
{
	const float n0 = R_SkyNoise(dir, time, 0.6f);
	const float n1 = R_SkyNoise(dir, time * 1.7f, 1.3f);
	float n = (n0 * 0.6f + n1 * 0.4f) * 0.5f + 0.5f;

	float alpha = Clamp((n - 0.55f) / 0.3f, 0.0f, 1.0f);
	alpha *= alpha;
	return alpha * SKY_CLOUD_ALPHA;
}

static float R_SkyRand01(uint* seed)
{
	*seed = (*seed * 1664525u) + 1013904223u;
	return (float)((*seed >> 8) & 0x00FFFFFF) * (1.0f / 16777216.0f);
}

static void R_DrawSkyClouds(void)
{
	const float time = r_newrefdef.time * SKY_CLOUD_SPEED;

	glDisable(GL_TEXTURE_2D);
	glEnable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	glDepthMask(GL_FALSE);

	for (int i = 0; i < 6; i++)
	{
		if (skymins[0][i] >= skymaxs[0][i] || skymins[1][i] >= skymaxs[1][i])
			continue;

		for (int y = 0; y < SKY_CLOUD_GRID; y++)
		{
			const float t0 = skymins[1][i] + (skymaxs[1][i] - skymins[1][i]) * ((float)y / (float)SKY_CLOUD_GRID);
			const float t1 = skymins[1][i] + (skymaxs[1][i] - skymins[1][i]) * ((float)(y + 1) / (float)SKY_CLOUD_GRID);

			glBegin(GL_TRIANGLE_STRIP);
			for (int x = 0; x <= SKY_CLOUD_GRID; x++)
			{
				const float s = skymins[0][i] + (skymaxs[0][i] - skymins[0][i]) * ((float)x / (float)SKY_CLOUD_GRID);

				vec3_t pos0;
				float s0, tt0;
				R_SkyVec(s, t0, i, pos0, &s0, &tt0);
				vec3_t dir0;
				VectorNormalize2(pos0, dir0);
				const float a0 = R_SkyCloudAlpha(dir0, time);
				glColor4f(1.0f, 1.0f, 1.0f, a0);
				glVertex3fv(pos0);

				vec3_t pos1;
				float s1, tt1;
				R_SkyVec(s, t1, i, pos1, &s1, &tt1);
				vec3_t dir1;
				VectorNormalize2(pos1, dir1);
				const float a1 = R_SkyCloudAlpha(dir1, time);
				glColor4f(1.0f, 1.0f, 1.0f, a1);
				glVertex3fv(pos1);
			}
			glEnd();
		}
	}

	glDepthMask(GL_TRUE);
	glDisable(GL_BLEND);
	glEnable(GL_TEXTURE_2D);
}

static void R_DrawSkyStars(void)
{
	const float clipdist = R_GetSkyClipDist();
    const float twinkle_time = r_newrefdef.time * 0.7f;
	uint seed = 0x1a2b3c4du;

	glDisable(GL_TEXTURE_2D);
	glEnable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE);
	glDepthMask(GL_FALSE);
	glPointSize(1.5f);

	glBegin(GL_POINTS);
	for (int i = 0; i < SKY_STAR_COUNT; i++)
	{
		const float u = R_SkyRand01(&seed);
		const float v = R_SkyRand01(&seed);
		const float w = R_SkyRand01(&seed);
		const float phase = R_SkyRand01(&seed) * (2.0f * (float)M_PI);

		const float theta = u * (2.0f * (float)M_PI);
		const float z = 0.2f + 0.8f * v;
		const float r = sqrtf(max(0.0f, 1.0f - z * z));

		vec3_t dir = { r * cosf(theta), r * sinf(theta), z };
		vec3_t pos;
		VectorScale(dir, clipdist, pos);

        const float base_intensity = 0.6f + 0.4f * w;
		const float twinkle = 0.75f + 0.25f * sinf(twinkle_time + phase);
		const float intensity = base_intensity * twinkle;
		glColor4f(intensity, intensity, intensity, 1.0f);
		glVertex3fv(pos);
	}
	glEnd();

	glDepthMask(GL_TRUE);
	glDisable(GL_BLEND);
	glEnable(GL_TEXTURE_2D);
}

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

static void R_MakeSkyVec(float s, float t, const int axis)
{
   vec3_t v;
	float s_coord;
	float t_coord;
	R_SkyVec(s, t, axis, v, &s_coord, &t_coord);
	glTexCoord2f(s_coord, t_coord);
	glVertex3fv(v);
}

void R_DrawSkyBox(void)
{
	static const int skytexorder[] = { 0, 2, 1, 3, 4, 5 }; //mxd. Made local static.

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

	glPushMatrix();
	glTranslatef(r_origin[0], r_origin[1], r_origin[2]);
	glRotatef(r_newrefdef.time * skyrotate, skyaxis[0], skyaxis[1], skyaxis[2]);

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
			R_BindImage(sky_images[skytexorder[i]]); // Q2: GL_Bind()

			glBegin(GL_QUADS);
			R_MakeSkyVec(skymins[0][i], skymins[1][i], i);
			R_MakeSkyVec(skymins[0][i], skymaxs[1][i], i);
			R_MakeSkyVec(skymaxs[0][i], skymaxs[1][i], i);
			R_MakeSkyVec(skymaxs[0][i], skymins[1][i], i);
			glEnd();
		}
	}

	R_DrawSkyStars();

	glPopMatrix();
}

void RI_SetSky(const char* name, const float rotate, const vec3_t axis)
{
	static const char* surf[] = { "rt", "bk", "lf", "ft", "up", "dn" }; // 3dstudio environment map names. //mxd. Made local static.

	skyrotate = rotate;
	VectorCopy(axis, skyaxis);

	for (int i = 0; i < 6; i++)
	{
		// H2: missing gl_skymip and qglColorTableEXT logic, 'env/%s%s.pcx' / 'env/%s%s.tga' -> 'pics/skies/%s%s.m8'
		sky_images[i] = R_FindImage(va("pics/skies/%s%s.m8", name, surf[i]), it_sky);

		if (skyrotate != 0.0f) // H2: gl_skymip -> gl_picmip //mxd. Removed gl_picmip cvar.
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