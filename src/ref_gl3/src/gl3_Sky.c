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

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#define MAX_CLIP_VERTS	64
#define ON_EPSILON		0.1f // Point on plane side epsilon.

#define SKY_CLOUD_GRID		8
#define SKY_CLOUD_ALPHA		0.35f
#define SKY_CLOUD_SPEED		0.02f
#define SKY_STAR_COUNT		400

#define SWAMP_LEAF_COUNT	60
#define SWAMP_DUST_COUNT	80
#define SWAMP_MIST_COUNT	25
#define SWAMP_LEAF_SIZE		2.5f
#define SWAMP_LEAF_FALL_SPEED	15.0f
#define SWAMP_LEAF_DRIFT		25.0f
#define SWAMP_DUST_SIZE		0.8f
#define SWAMP_DUST_SPEED	8.0f
#define SWAMP_MIST_MIN_SIZE	60.0f
#define SWAMP_MIST_MAX_SIZE	150.0f
#define SWAMP_MIST_DRIFT	3.0f

typedef struct
{
	vec3_t pos;
	vec3_t velocity;
	float rotation;
	float rot_speed;
	float lifetime;
	float alpha;
} swamp_leaf_t;

typedef struct
{
	vec3_t pos;
	vec3_t velocity;
	float lifetime;
	float alpha;
	float phase;
} swamp_dust_t;

typedef struct
{
	vec3_t pos;
	vec3_t velocity;
	float size;
	float rotation;
	float rot_speed;
	float phase;
	float alpha_base;
	float height_offset;
} swamp_mist_t;

static float skyrotate;
static vec3_t skyaxis;
static image_t* sky_images[6];

static swamp_leaf_t swamp_leaves[SWAMP_LEAF_COUNT];
static swamp_dust_t swamp_dust[SWAMP_DUST_COUNT];
static swamp_mist_t swamp_mist[SWAMP_MIST_COUNT];
static qboolean swamp_particles_initialized = false;

static float skymins[2][6];
static float skymaxs[2][6];
static float sky_min;
static float sky_max;

static void R_MakeSkyVec(float s, float t, const int axis, vec3_t out_pos, float* out_s, float* out_t);

static float R_GetSkyClipDist(void)
{
	if ((int)r_fog->value)
		return r_farclipdist->value;

	return r_farclipdist->value * 0.5773503f;
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

	glBindTexture(GL_TEXTURE_2D, gl3state.whiteTexture);
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

			float quad[(SKY_CLOUD_GRID + 1) * 2 * 9];
			int vert = 0;

			for (int x = 0; x <= SKY_CLOUD_GRID; x++)
			{
				const float s = skymins[0][i] + (skymaxs[0][i] - skymins[0][i]) * ((float)x / (float)SKY_CLOUD_GRID);

				vec3_t pos0;
				float s0, t0c;
				R_MakeSkyVec(s, t0, i, pos0, &s0, &t0c);
				vec3_t dir0;
				VectorNormalize2(pos0, dir0);
				const float a0 = R_SkyCloudAlpha(dir0, time);

				quad[vert++] = pos0[0];
				quad[vert++] = pos0[1];
				quad[vert++] = pos0[2];
				quad[vert++] = 0.0f;
				quad[vert++] = 0.0f;
				quad[vert++] = 1.0f;
				quad[vert++] = 1.0f;
				quad[vert++] = 1.0f;
				quad[vert++] = a0;

				vec3_t pos1;
				float s1, t1c;
				R_MakeSkyVec(s, t1, i, pos1, &s1, &t1c);
				vec3_t dir1;
				VectorNormalize2(pos1, dir1);
				const float a1 = R_SkyCloudAlpha(dir1, time);

				quad[vert++] = pos1[0];
				quad[vert++] = pos1[1];
				quad[vert++] = pos1[2];
				quad[vert++] = 0.0f;
				quad[vert++] = 0.0f;
				quad[vert++] = 1.0f;
				quad[vert++] = 1.0f;
				quad[vert++] = 1.0f;
				quad[vert++] = a1;
			}

			GL3_Draw3DPoly(GL_TRIANGLE_STRIP, quad, vert / 9);
		}
	}

	glDepthMask(GL_TRUE);
	glDisable(GL_BLEND);
}

static qboolean R_IsSilverpringMap(void)
{
	if (r_worldmodel == NULL || r_worldmodel->name[0] == '\0')
		return false;

	// Check if the map name contains any of the Silverpring level identifiers.
	// Silverpring levels: ssdocks, sswarehouse, sstown, sspalace
	return (strstr(r_worldmodel->name, "ssdocks") != NULL ||
			strstr(r_worldmodel->name, "sswarehouse") != NULL ||
			strstr(r_worldmodel->name, "sstown") != NULL ||
			strstr(r_worldmodel->name, "sspalace") != NULL);
}

static qboolean R_IsDarkmireSwampMap(void)
{
	if (r_worldmodel == NULL || r_worldmodel->name[0] == '\0')
		return false;

	return (strstr(r_worldmodel->name, "dmireswamp") != NULL);
}

static void R_InitSwampLeaf(swamp_leaf_t* leaf, const float clipdist, uint* seed)
{
	const float angle = R_SkyRand01(seed) * 2.0f * (float)M_PI;
	const float radius = R_SkyRand01(seed) * 800.0f; // Spread across level

	// Position relative to player (in world space)
	leaf->pos[0] = r_origin[0] + cosf(angle) * radius;
	leaf->pos[1] = r_origin[1] + sinf(angle) * radius;
	leaf->pos[2] = r_origin[2] + (R_SkyRand01(seed) - 0.2f) * 400.0f; // Height variation

	leaf->velocity[0] = (R_SkyRand01(seed) - 0.5f) * SWAMP_LEAF_DRIFT;
	leaf->velocity[1] = (R_SkyRand01(seed) - 0.5f) * SWAMP_LEAF_DRIFT;
	leaf->velocity[2] = -SWAMP_LEAF_FALL_SPEED * (0.8f + R_SkyRand01(seed) * 0.4f);

	leaf->rotation = R_SkyRand01(seed) * 360.0f;
	leaf->rot_speed = (R_SkyRand01(seed) - 0.5f) * 90.0f;
	leaf->lifetime = 0.0f;
	leaf->alpha = 0.0f;
}

static void R_InitSwampDust(swamp_dust_t* dust, const float clipdist, uint* seed)
{
	const float angle = R_SkyRand01(seed) * 2.0f * (float)M_PI;
	const float radius = R_SkyRand01(seed) * 600.0f; // Closer to player than leaves

	// Position relative to player (in world space)
	dust->pos[0] = r_origin[0] + cosf(angle) * radius;
	dust->pos[1] = r_origin[1] + sinf(angle) * radius;
	dust->pos[2] = r_origin[2] + (R_SkyRand01(seed) - 0.5f) * 300.0f; // Height variation

	dust->velocity[0] = (R_SkyRand01(seed) - 0.5f) * SWAMP_DUST_SPEED;
	dust->velocity[1] = (R_SkyRand01(seed) - 0.5f) * SWAMP_DUST_SPEED;
	dust->velocity[2] = (R_SkyRand01(seed) - 0.5f) * SWAMP_DUST_SPEED * 0.5f;

	dust->phase = R_SkyRand01(seed) * 2.0f * (float)M_PI;
	dust->lifetime = 0.0f;
	dust->alpha = 0.0f;
}

static void R_InitSwampMist(swamp_mist_t* mist, uint* seed)
{
	const float angle = R_SkyRand01(seed) * 2.0f * (float)M_PI;
	const float radius = 100.0f + R_SkyRand01(seed) * 500.0f;

	// Position relative to player, floating low above ground.
	mist->pos[0] = r_origin[0] + cosf(angle) * radius;
	mist->pos[1] = r_origin[1] + sinf(angle) * radius;
	mist->height_offset = -80.0f + R_SkyRand01(seed) * 100.0f; // -80 to +20 relative to player.
	mist->pos[2] = r_origin[2] + mist->height_offset;

	// Slow drifting velocity.
	mist->velocity[0] = (R_SkyRand01(seed) - 0.5f) * SWAMP_MIST_DRIFT;
	mist->velocity[1] = (R_SkyRand01(seed) - 0.5f) * SWAMP_MIST_DRIFT;
	mist->velocity[2] = (R_SkyRand01(seed) - 0.5f) * SWAMP_MIST_DRIFT * 0.3f;

	// Random size.
	mist->size = SWAMP_MIST_MIN_SIZE + R_SkyRand01(seed) * (SWAMP_MIST_MAX_SIZE - SWAMP_MIST_MIN_SIZE);

	// Slow rotation.
	mist->rotation = R_SkyRand01(seed) * 360.0f;
	mist->rot_speed = (R_SkyRand01(seed) - 0.5f) * 10.0f;

	// Phase for pulsing effect.
	mist->phase = R_SkyRand01(seed) * 2.0f * (float)M_PI;

	// Base alpha (subtle).
	mist->alpha_base = 0.08f + R_SkyRand01(seed) * 0.07f;
}

static void R_InitSwampParticles(void)
{
	const float clipdist = R_GetSkyClipDist();
	uint seed = 0x5ca1ab1eu;

	for (int i = 0; i < SWAMP_LEAF_COUNT; i++)
		R_InitSwampLeaf(&swamp_leaves[i], clipdist, &seed);

	for (int i = 0; i < SWAMP_DUST_COUNT; i++)
		R_InitSwampDust(&swamp_dust[i], clipdist, &seed);

	for (int i = 0; i < SWAMP_MIST_COUNT; i++)
		R_InitSwampMist(&swamp_mist[i], &seed);

	swamp_particles_initialized = true;
}

static void R_UpdateSwampLeaves(const float dt, uint* seed)
{
	const float clipdist = R_GetSkyClipDist();
	const float time = r_newrefdef.time;

	for (int i = 0; i < SWAMP_LEAF_COUNT; i++)
	{
		swamp_leaf_t* leaf = &swamp_leaves[i];

		leaf->lifetime += dt;

		// Fade in at start
		if (leaf->lifetime < 1.0f)
			leaf->alpha = leaf->lifetime;
		else
			leaf->alpha = 1.0f;

		// Wind drift (sinusoidal)
		const float wind_x = sinf(time * 0.5f + (float)i * 0.1f) * 5.0f;
		const float wind_y = cosf(time * 0.3f + (float)i * 0.15f) * 5.0f;

		// Update position
		leaf->pos[0] += (leaf->velocity[0] + wind_x) * dt;
		leaf->pos[1] += (leaf->velocity[1] + wind_y) * dt;
		leaf->pos[2] += leaf->velocity[2] * dt;

		// Update rotation
		leaf->rotation += leaf->rot_speed * dt;

		// Respawn if too far from player or fallen below
		const float dx = leaf->pos[0] - r_origin[0];
		const float dy = leaf->pos[1] - r_origin[1];
		const float dz = leaf->pos[2] - r_origin[2];
		const float dist_sq = dx * dx + dy * dy;

		if (dist_sq > 1000.0f * 1000.0f || dz < -200.0f)
			R_InitSwampLeaf(leaf, clipdist, seed);
	}
}

static void R_UpdateSwampDust(const float dt, uint* seed)
{
	const float clipdist = R_GetSkyClipDist();
	const float time = r_newrefdef.time;

	for (int i = 0; i < SWAMP_DUST_COUNT; i++)
	{
		swamp_dust_t* dust = &swamp_dust[i];

		dust->lifetime += dt;

		// Fade in/out cycle
		const float cycle = fmodf(dust->lifetime, 8.0f);
		if (cycle < 2.0f)
			dust->alpha = cycle * 0.5f;
		else if (cycle > 6.0f)
			dust->alpha = (8.0f - cycle) * 0.5f;
		else
			dust->alpha = 1.0f;

		// Swirling motion
		const float swirl_time = time + dust->phase;
		const float swirl_x = sinf(swirl_time * 0.8f) * 3.0f;
		const float swirl_y = cosf(swirl_time * 0.6f) * 3.0f;
		const float swirl_z = sinf(swirl_time * 0.4f) * 2.0f;

		// Update position
		dust->pos[0] += (dust->velocity[0] + swirl_x) * dt;
		dust->pos[1] += (dust->velocity[1] + swirl_y) * dt;
		dust->pos[2] += (dust->velocity[2] + swirl_z) * dt;

		// Respawn if too far from player
		const float dx = dust->pos[0] - r_origin[0];
		const float dy = dust->pos[1] - r_origin[1];
		const float dz = dust->pos[2] - r_origin[2];
		const float dist_sq = dx * dx + dy * dy;

		if (dist_sq > 800.0f * 800.0f || fabsf(dz) > 400.0f)
			R_InitSwampDust(dust, clipdist, seed);
	}
}

static void R_UpdateSwampMist(const float dt, uint* seed)
{
	const float time = r_newrefdef.time;

	for (int i = 0; i < SWAMP_MIST_COUNT; i++)
	{
		swamp_mist_t* mist = &swamp_mist[i];

		// Slow undulating drift.
		const float drift_time = time * 0.3f + mist->phase;
		const float drift_x = sinf(drift_time * 0.7f) * 2.0f;
		const float drift_y = cosf(drift_time * 0.5f) * 2.0f;
		const float drift_z = sinf(drift_time * 0.3f) * 0.5f;

		// Update position.
		mist->pos[0] += (mist->velocity[0] + drift_x) * dt;
		mist->pos[1] += (mist->velocity[1] + drift_y) * dt;
		mist->pos[2] += (mist->velocity[2] + drift_z) * dt;

		// Update rotation.
		mist->rotation += mist->rot_speed * dt;

		// Respawn if too far from player.
		const float dx = mist->pos[0] - r_origin[0];
		const float dy = mist->pos[1] - r_origin[1];
		const float dist_sq = dx * dx + dy * dy;

		if (dist_sq > 700.0f * 700.0f)
			R_InitSwampMist(mist, seed);
	}
}

static void R_DrawSkyStars(void)
{
	const float clipdist = R_GetSkyClipDist();
	const float twinkle_time = r_newrefdef.time * 0.7f;
	uint seed = 0x1a2b3c4du;

	glBindTexture(GL_TEXTURE_2D, gl3state.whiteTexture);
	glEnable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE);
	glDepthMask(GL_FALSE);
	glPointSize(2.0f);

	float verts[SKY_STAR_COUNT * 9];
	int vert = 0;

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

		verts[vert++] = pos[0];
		verts[vert++] = pos[1];
		verts[vert++] = pos[2];
		verts[vert++] = 0.0f;
		verts[vert++] = 0.0f;
		verts[vert++] = intensity;
		verts[vert++] = intensity;
		verts[vert++] = intensity;
		verts[vert++] = 1.0f;
	}

	GL3_Draw3DPoly(GL_POINTS, verts, SKY_STAR_COUNT);

	glDepthMask(GL_TRUE);
	glDisable(GL_BLEND);
}

//mxd. Northern lights (aurora borealis) effect for night sky.
#define AURORA_BAND_COUNT		5
#define AURORA_SEGMENTS			48

static void R_DrawSkyAurora(void)
{
	const float clipdist = R_GetSkyClipDist();
	const float time = r_newrefdef.time;

	// Aurora colors: greens, teals, blues, purples.
	static const vec3_t aurora_colors[AURORA_BAND_COUNT] =
	{
		{ 0.2f, 0.9f, 0.3f },	// Green.
		{ 0.15f, 0.8f, 0.5f },	// Teal-green.
		{ 0.1f, 0.7f, 0.6f },	// Teal.
		{ 0.3f, 0.5f, 0.9f },	// Blue.
		{ 0.5f, 0.3f, 0.7f }	// Purple.
	};

	// Band parameters: z_base (height 0-1), curtain_height, wave_amplitude, phase.
	static const float aurora_params[AURORA_BAND_COUNT][4] =
	{
		{ 0.42f, 0.20f, 0.04f, 0.0f },
		{ 0.48f, 0.18f, 0.035f, 0.8f },
		{ 0.54f, 0.16f, 0.03f, 1.6f },
		{ 0.60f, 0.14f, 0.025f, 2.4f },
		{ 0.66f, 0.12f, 0.02f, 3.2f }
	};

	glBindTexture(GL_TEXTURE_2D, gl3state.whiteTexture);
	glEnable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE); // Additive blending for glow effect.
	glDepthMask(GL_FALSE);

	for (int band = 0; band < AURORA_BAND_COUNT; band++)
	{
		const float z_base = aurora_params[band][0];
		const float curtain_height = aurora_params[band][1];
		const float wave_amp = aurora_params[band][2];
		const float phase = aurora_params[band][3];

		// Slow global pulse for this band.
		const float pulse = 0.5f + 0.5f * sinf(time * 0.15f + phase);
		const float base_alpha = 0.12f * pulse; // More transparent.

		// Build vertex data for a quad strip (2 verts per segment).
		float verts[(AURORA_SEGMENTS + 1) * 2 * 9];
		int vert = 0;

		for (int seg = 0; seg <= AURORA_SEGMENTS; seg++)
		{
			// Full 360 degree sweep.
			const float t = (float)seg / (float)AURORA_SEGMENTS;
			const float theta = t * 2.0f * (float)M_PI;

			// Multi-frequency wave for more organic look.
			const float wave1 = sinf(t * 6.0f * (float)M_PI + time * 0.3f + phase) * wave_amp;
			const float wave2 = sinf(t * 10.0f * (float)M_PI + time * 0.5f + phase * 1.5f) * wave_amp * 0.5f;
			const float wave = wave1 + wave2;

			// Variable curtain height along the band.
			const float height_var = curtain_height * (0.7f + 0.3f * sinf(t * 4.0f * (float)M_PI + time * 0.2f));

			// Z heights for top and bottom of curtain.
			const float z_top = min(z_base + height_var + wave, 0.99f);
			const float z_bot = max(z_base + wave, 0.01f);

			// Calculate horizontal radius from z (unit sphere).
			const float r_top = sqrtf(max(0.0f, 1.0f - z_top * z_top));
			const float r_bot = sqrtf(max(0.0f, 1.0f - z_bot * z_bot));

			// Smooth shimmer effect.
			const float shimmer = 0.6f + 0.4f * sinf(t * 12.0f * (float)M_PI + time * 1.5f + phase);
			const float alpha = base_alpha * shimmer;

			// Top vertex (very faded).
			verts[vert++] = r_top * cosf(theta) * clipdist;
			verts[vert++] = r_top * sinf(theta) * clipdist;
			verts[vert++] = z_top * clipdist;
			verts[vert++] = 0.0f;
			verts[vert++] = 0.0f;
			verts[vert++] = aurora_colors[band][0];
			verts[vert++] = aurora_colors[band][1];
			verts[vert++] = aurora_colors[band][2];
			verts[vert++] = alpha * 0.05f;

			// Bottom vertex (brighter but still subtle).
			verts[vert++] = r_bot * cosf(theta) * clipdist;
			verts[vert++] = r_bot * sinf(theta) * clipdist;
			verts[vert++] = z_bot * clipdist;
			verts[vert++] = 0.0f;
			verts[vert++] = 0.0f;
			verts[vert++] = aurora_colors[band][0];
			verts[vert++] = aurora_colors[band][1];
			verts[vert++] = aurora_colors[band][2];
			verts[vert++] = alpha;
		}

		GL3_Draw3DPoly(GL_TRIANGLE_STRIP, verts, (AURORA_SEGMENTS + 1) * 2);
	}

	glDepthMask(GL_TRUE);
	glDisable(GL_BLEND);
}

static void R_DrawSwampLeaves(void)
{
	glBindTexture(GL_TEXTURE_2D, gl3state.whiteTexture);
	glEnable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	glDepthMask(GL_FALSE);

	// Brownish-green leaf color
	const vec3_t leaf_color = { 0.4f, 0.3f, 0.15f };

	for (int i = 0; i < SWAMP_LEAF_COUNT; i++)
	{
		const swamp_leaf_t* leaf = &swamp_leaves[i];

		if (leaf->alpha < 0.01f)
			continue;

		const float size = SWAMP_LEAF_SIZE;
		const float rot_rad = leaf->rotation * ((float)M_PI / 180.0f);
		const float cos_r = cosf(rot_rad);
		const float sin_r = sinf(rot_rad);

		// Compute billboard corners with rotation
		vec3_t right = { cos_r * size, sin_r * size, 0.0f };
		vec3_t up = { -sin_r * size, cos_r * size, 0.0f };

		const float alpha = leaf->alpha * 0.8f;

		float quad[4 * 9];
		int vert = 0;

		// Bottom-left
		quad[vert++] = leaf->pos[0] - right[0] - up[0];
		quad[vert++] = leaf->pos[1] - right[1] - up[1];
		quad[vert++] = leaf->pos[2] - right[2] - up[2];
		quad[vert++] = 0.0f;
		quad[vert++] = 0.0f;
		quad[vert++] = leaf_color[0];
		quad[vert++] = leaf_color[1];
		quad[vert++] = leaf_color[2];
		quad[vert++] = alpha;

		// Bottom-right
		quad[vert++] = leaf->pos[0] + right[0] - up[0];
		quad[vert++] = leaf->pos[1] + right[1] - up[1];
		quad[vert++] = leaf->pos[2] + right[2] - up[2];
		quad[vert++] = 0.0f;
		quad[vert++] = 0.0f;
		quad[vert++] = leaf_color[0];
		quad[vert++] = leaf_color[1];
		quad[vert++] = leaf_color[2];
		quad[vert++] = alpha;

		// Top-right
		quad[vert++] = leaf->pos[0] + right[0] + up[0];
		quad[vert++] = leaf->pos[1] + right[1] + up[1];
		quad[vert++] = leaf->pos[2] + right[2] + up[2];
		quad[vert++] = 0.0f;
		quad[vert++] = 0.0f;
		quad[vert++] = leaf_color[0];
		quad[vert++] = leaf_color[1];
		quad[vert++] = leaf_color[2];
		quad[vert++] = alpha;

		// Top-left
		quad[vert++] = leaf->pos[0] - right[0] + up[0];
		quad[vert++] = leaf->pos[1] - right[1] + up[1];
		quad[vert++] = leaf->pos[2] - right[2] + up[2];
		quad[vert++] = 0.0f;
		quad[vert++] = 0.0f;
		quad[vert++] = leaf_color[0];
		quad[vert++] = leaf_color[1];
		quad[vert++] = leaf_color[2];
		quad[vert++] = alpha;

		GL3_Draw3DPoly(GL_TRIANGLE_FAN, quad, 4);
	}

	glDepthMask(GL_TRUE);
	glDisable(GL_BLEND);
}

static void R_DrawSwampDust(void)
{
	glBindTexture(GL_TEXTURE_2D, gl3state.whiteTexture);
	glEnable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	glDepthMask(GL_FALSE);
	glPointSize(SWAMP_DUST_SIZE);

	// Misty gray-green dust color
	const vec3_t dust_color = { 0.6f, 0.65f, 0.55f };

	float verts[SWAMP_DUST_COUNT * 9];
	int vert = 0;
	int count = 0;

	for (int i = 0; i < SWAMP_DUST_COUNT; i++)
	{
		const swamp_dust_t* dust = &swamp_dust[i];

		if (dust->alpha < 0.01f)
			continue;

		verts[vert++] = dust->pos[0];
		verts[vert++] = dust->pos[1];
		verts[vert++] = dust->pos[2];
		verts[vert++] = 0.0f;
		verts[vert++] = 0.0f;
		verts[vert++] = dust_color[0];
		verts[vert++] = dust_color[1];
		verts[vert++] = dust_color[2];
		verts[vert++] = dust->alpha * 0.3f;
		count++;
	}

	if (count > 0)
		GL3_Draw3DPoly(GL_POINTS, verts, count);

	glDepthMask(GL_TRUE);
	glDisable(GL_BLEND);
}

static void R_DrawSwampMist(void)
{
	const float time = r_newrefdef.time;

	glBindTexture(GL_TEXTURE_2D, gl3state.whiteTexture);
	glEnable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	glDepthMask(GL_FALSE);

	// Swampy green mist color.
	const vec3_t mist_color = { 0.3f, 0.5f, 0.25f };

	for (int i = 0; i < SWAMP_MIST_COUNT; i++)
	{
		const swamp_mist_t* mist = &swamp_mist[i];

		// Pulsing alpha effect.
		const float pulse = 0.7f + 0.3f * sinf(time * 0.4f + mist->phase);
		const float alpha = mist->alpha_base * pulse;

		// Distance-based fade (fade out when close to avoid popping).
		const float dx = mist->pos[0] - r_origin[0];
		const float dy = mist->pos[1] - r_origin[1];
		const float dist = sqrtf(dx * dx + dy * dy);
		const float dist_fade = Clamp((dist - 50.0f) / 150.0f, 0.0f, 1.0f);

		const float final_alpha = alpha * dist_fade;
		if (final_alpha < 0.005f)
			continue;

		const float size = mist->size;
		const float rot_rad = mist->rotation * ((float)M_PI / 180.0f);
		const float cos_r = cosf(rot_rad);
		const float sin_r = sinf(rot_rad);

		// Billboard quad oriented in XY plane (horizontal mist).
		vec3_t right = { cos_r * size, sin_r * size, 0.0f };
		vec3_t fwd = { -sin_r * size, cos_r * size, 0.0f };

		float quad[4 * 9];
		int vert = 0;

		// Draw as a soft, faded quad with center brighter than edges.
		// We'll draw multiple overlapping smaller quads for a softer look.

		// Main quad (full size, low alpha).
		quad[vert++] = mist->pos[0] - right[0] - fwd[0];
		quad[vert++] = mist->pos[1] - right[1] - fwd[1];
		quad[vert++] = mist->pos[2];
		quad[vert++] = 0.0f;
		quad[vert++] = 0.0f;
		quad[vert++] = mist_color[0];
		quad[vert++] = mist_color[1];
		quad[vert++] = mist_color[2];
		quad[vert++] = final_alpha * 0.3f;

		quad[vert++] = mist->pos[0] + right[0] - fwd[0];
		quad[vert++] = mist->pos[1] + right[1] - fwd[1];
		quad[vert++] = mist->pos[2];
		quad[vert++] = 0.0f;
		quad[vert++] = 0.0f;
		quad[vert++] = mist_color[0];
		quad[vert++] = mist_color[1];
		quad[vert++] = mist_color[2];
		quad[vert++] = final_alpha * 0.3f;

		quad[vert++] = mist->pos[0] + right[0] + fwd[0];
		quad[vert++] = mist->pos[1] + right[1] + fwd[1];
		quad[vert++] = mist->pos[2];
		quad[vert++] = 0.0f;
		quad[vert++] = 0.0f;
		quad[vert++] = mist_color[0];
		quad[vert++] = mist_color[1];
		quad[vert++] = mist_color[2];
		quad[vert++] = final_alpha * 0.3f;

		quad[vert++] = mist->pos[0] - right[0] + fwd[0];
		quad[vert++] = mist->pos[1] - right[1] + fwd[1];
		quad[vert++] = mist->pos[2];
		quad[vert++] = 0.0f;
		quad[vert++] = 0.0f;
		quad[vert++] = mist_color[0];
		quad[vert++] = mist_color[1];
		quad[vert++] = mist_color[2];
		quad[vert++] = final_alpha * 0.3f;

		GL3_Draw3DPoly(GL_TRIANGLE_FAN, quad, 4);

		// Inner core (smaller, brighter).
		const float inner_scale = 0.5f;
		vert = 0;

		quad[vert++] = mist->pos[0] - right[0] * inner_scale - fwd[0] * inner_scale;
		quad[vert++] = mist->pos[1] - right[1] * inner_scale - fwd[1] * inner_scale;
		quad[vert++] = mist->pos[2];
		quad[vert++] = 0.0f;
		quad[vert++] = 0.0f;
		quad[vert++] = mist_color[0];
		quad[vert++] = mist_color[1];
		quad[vert++] = mist_color[2];
		quad[vert++] = final_alpha * 0.5f;

		quad[vert++] = mist->pos[0] + right[0] * inner_scale - fwd[0] * inner_scale;
		quad[vert++] = mist->pos[1] + right[1] * inner_scale - fwd[1] * inner_scale;
		quad[vert++] = mist->pos[2];
		quad[vert++] = 0.0f;
		quad[vert++] = 0.0f;
		quad[vert++] = mist_color[0];
		quad[vert++] = mist_color[1];
		quad[vert++] = mist_color[2];
		quad[vert++] = final_alpha * 0.5f;

		quad[vert++] = mist->pos[0] + right[0] * inner_scale + fwd[0] * inner_scale;
		quad[vert++] = mist->pos[1] + right[1] * inner_scale + fwd[1] * inner_scale;
		quad[vert++] = mist->pos[2];
		quad[vert++] = 0.0f;
		quad[vert++] = 0.0f;
		quad[vert++] = mist_color[0];
		quad[vert++] = mist_color[1];
		quad[vert++] = mist_color[2];
		quad[vert++] = final_alpha * 0.5f;

		quad[vert++] = mist->pos[0] - right[0] * inner_scale + fwd[0] * inner_scale;
		quad[vert++] = mist->pos[1] - right[1] * inner_scale + fwd[1] * inner_scale;
		quad[vert++] = mist->pos[2];
		quad[vert++] = 0.0f;
		quad[vert++] = 0.0f;
		quad[vert++] = mist_color[0];
		quad[vert++] = mist_color[1];
		quad[vert++] = mist_color[2];
		quad[vert++] = final_alpha * 0.5f;

		GL3_Draw3DPoly(GL_TRIANGLE_FAN, quad, 4);
	}

	glDepthMask(GL_TRUE);
	glDisable(GL_BLEND);
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

	if (sky_images[0] == NULL)
		return;

	// Disable depth writes to ensure skybox is always drawn behind everything.
	glDepthMask(GL_FALSE);

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

	// Sky panels must be visible from both the normal pass (glCullFace GL_FRONT)
	// and the reflection pass (glCullFace GL_BACK). Disable face culling for the
	// duration so the panels are never back-face-culled away.
	GLboolean cull_was_enabled;
	glGetBooleanv(GL_CULL_FACE, &cull_was_enabled);
	glDisable(GL_CULL_FACE);

	for (int i = 0; i < 6; i++)
	{
		// Always force full sky to draw to avoid culling artifacts.
		skymins[0][i] = -1.0f;
		skymins[1][i] = -1.0f;
		skymaxs[0][i] = 1.0f;
		skymaxs[1][i] = 1.0f;

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

	if (cull_was_enabled)
		glEnable(GL_CULL_FACE);

	if (R_IsSilverpringMap())
	{
		R_DrawSkyAurora();
		R_DrawSkyStars();
	}

	// GL3: Restore world modelview matrix (equivalent of glPopMatrix).
	GL3_UpdateModelview3D(r_world_matrix);

	// Restore depth writes.
	glDepthMask(GL_TRUE);

	// Draw swamp particles for Darkmire Swamp level (in world space, not sky space)
	if (R_IsDarkmireSwampMap())
	{
		static float last_time = 0.0f;
		const float current_time = r_newrefdef.time;
		const float dt = (last_time > 0.0f) ? (current_time - last_time) : 0.016f;
		last_time = current_time;

		if (!swamp_particles_initialized)
			R_InitSwampParticles();

		uint seed = 0x5ca1ab1eu;
		R_UpdateSwampLeaves(dt, &seed);
		R_UpdateSwampDust(dt, &seed);
		R_UpdateSwampMist(dt, &seed);

		R_DrawSwampMist();  // Draw mist first (behind other particles).
		R_DrawSwampLeaves();
		R_DrawSwampDust();
	}
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
