//
// gl3_Lightmap.c
//
// OpenGL 3.3 lightmap management.
// Based on gl1_Lightmap.c -- Copyright 1998 Raven Software
//

#include "gl3_Lightmap.h"
#include "gl3_Image.h"
#include "gl3_Light.h"
#include "Hunk.h"
#include "Vector.h"

gllightmapstate_t gl_lms;

#pragma region ========================== LIGHTMAP ALLOCATION ==========================

void LM_InitBlock(void)
{
	memset(gl_lms.allocated, 0, sizeof(gl_lms.allocated));
}

void LM_UploadBlock(const qboolean dynamic)
{
	// In GL3 we store actual GL texture IDs in gl3state.lightmap_textures[].
	R_Bind(gl3state.lightmap_textures[dynamic ? 0 : gl_lms.current_lightmap_texture]);

	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

	if (dynamic)
	{
		int height = 0;
		for (int i = 0; i < LM_BLOCK_WIDTH; i++)
			height = max(gl_lms.allocated[i], height);

		glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, LM_BLOCK_WIDTH, height, GL_LIGHTMAP_FORMAT, GL_UNSIGNED_BYTE, gl_lms.lightmap_buffer);
	}
	else
	{
		glTexImage2D(GL_TEXTURE_2D, 0, gl_lms.internal_format, LM_BLOCK_WIDTH, LM_BLOCK_HEIGHT, 0, GL_LIGHTMAP_FORMAT, GL_UNSIGNED_BYTE, gl_lms.lightmap_buffer);
		gl_lms.current_lightmap_texture++;

		if (gl_lms.current_lightmap_texture == MAX_LIGHTMAPS)
			ri.Sys_Error(ERR_DROP, "LM_UploadBlock() - MAX_LIGHTMAPS exceeded\n");
	}
}

qboolean LM_AllocBlock(const int w, const int h, int* x, int* y)
{
	int j;
	int best = LM_BLOCK_HEIGHT;

	for (int i = 0; i < LM_BLOCK_WIDTH - w; i++)
	{
		int best2 = 0;

		for (j = 0; j < w; j++)
		{
			if (gl_lms.allocated[i + j] >= best)
				break;

			if (gl_lms.allocated[i + j] > best2)
				best2 = gl_lms.allocated[i + j];
		}

		if (j == w)
		{
			*x = i;
			*y = best2;
			best = best2;
		}
	}

	if (best + h > LM_BLOCK_HEIGHT)
		return false;

	for (int i = 0; i < w; i++)
		gl_lms.allocated[*x + i] = best + h;

	return true;
}

#pragma endregion

#pragma region ========================== LIGHTMAP BUILDING ==========================

void LM_BuildPolygonFromSurface(const model_t* mdl, msurface_t* fa)
{
	const medge_t* pedges = mdl->edges;
	const int lnumverts = fa->numedges;

	glpoly_t* poly = Hunk_Alloc((int)sizeof(glpoly_t) + (lnumverts - 4) * VERTEXSIZE * sizeof(float));
	poly->next = fa->polys;
	poly->flags = fa->flags;
	fa->polys = poly;
	poly->numverts = lnumverts;

	for (int i = 0; i < lnumverts; i++)
	{
		const int lindex = mdl->surfedges[fa->firstedge + i];

		float* vec;
		if (lindex > 0)
		{
			const medge_t* r_pedge = &pedges[lindex];
			vec = mdl->vertexes[r_pedge->v[0]].position;
		}
		else
		{
			const medge_t* r_pedge = &pedges[-lindex];
			vec = mdl->vertexes[r_pedge->v[1]].position;
		}

		float s = DotProduct(vec, fa->texinfo->vecs[0]) + fa->texinfo->vecs[0][3];
		s /= (float)fa->texinfo->image->width;

		float t = DotProduct(vec, fa->texinfo->vecs[1]) + fa->texinfo->vecs[1][3];
		t /= (float)fa->texinfo->image->height;

		VectorCopy(vec, poly->verts[i]);
		poly->verts[i][3] = s;
		poly->verts[i][4] = t;

		// Lightmap texture coordinates.
		s = DotProduct(vec, fa->texinfo->vecs[0]) + fa->texinfo->vecs[0][3];
		s -= (float)fa->texturemins[0];
		s += (float)fa->light_s * 16;
		s += 8;
		s /= LM_BLOCK_WIDTH * 16;

		t = DotProduct(vec, fa->texinfo->vecs[1]) + fa->texinfo->vecs[1][3];
		t -= (float)fa->texturemins[1];
		t += (float)fa->light_t * 16;
		t += 8;
		t /= LM_BLOCK_HEIGHT * 16;

		poly->verts[i][5] = s;
		poly->verts[i][6] = t;
	}
}

void LM_CreateSurfaceLightmap(msurface_t* surf)
{
	if (surf->flags & (SURF_DRAWSKY | SURF_DRAWTURB))
		return;

	const int smax = ((int)surf->extents[0] >> 4) + 1;
	const int tmax = ((int)surf->extents[1] >> 4) + 1;

	if (!LM_AllocBlock(smax, tmax, &surf->light_s, &surf->light_t))
	{
		LM_UploadBlock(false);
		LM_InitBlock();

		if (!LM_AllocBlock(smax, tmax, &surf->light_s, &surf->light_t))
			ri.Sys_Error(ERR_FATAL, "Consecutive calls to LM_AllocBlock(%d,%d) failed\n", smax, tmax);
	}

	surf->lightmaptexturenum = gl_lms.current_lightmap_texture;

	byte* base = gl_lms.lightmap_buffer;
	base += (surf->light_t * LM_BLOCK_WIDTH + surf->light_s) * LIGHTMAP_BYTES;

	R_SetCacheState(surf);
	R_BuildLightMap(surf, base, LM_BLOCK_WIDTH * LIGHTMAP_BYTES);
}

void LM_BeginBuildingLightmaps(void)
{
	static lightstyle_t lightstyles[MAX_LIGHTSTYLES];
	static uint dummy[LM_BLOCK_WIDTH * LM_BLOCK_HEIGHT];

	memset(gl_lms.allocated, 0, sizeof(gl_lms.allocated));

	r_framecount = 1; // No dlightcache.

	// Select lightmap texture unit.
	R_SelectTexture(GL_TEXTURE1);

	// Setup the base lightstyles so lightmaps won't have to be regenerated on first view.
	for (int i = 0; i < MAX_LIGHTSTYLES; i++)
	{
		lightstyles[i].rgb[0] = 1.0f;
		lightstyles[i].rgb[1] = 1.0f;
		lightstyles[i].rgb[2] = 1.0f;
		lightstyles[i].white = 3.0f;
	}

	r_newrefdef.lightstyles = lightstyles;

	gl_lms.current_lightmap_texture = 1;
	gl_lms.internal_format = GL_TEX_SOLID_FORMAT;

	// Generate GL texture objects for all lightmap slots if not yet done.
	if (gl3state.lightmap_textures[0] == 0)
		glGenTextures(MAX_LIGHTMAPS + 1, gl3state.lightmap_textures);

	// Initialize the dynamic lightmap texture (slot 0).
	R_Bind(gl3state.lightmap_textures[0]);

	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTexImage2D(GL_TEXTURE_2D, 0, gl_lms.internal_format, LM_BLOCK_WIDTH, LM_BLOCK_HEIGHT, 0, GL_LIGHTMAP_FORMAT, GL_UNSIGNED_BYTE, dummy);
}

void LM_EndBuildingLightmaps(void)
{
	LM_UploadBlock(false);
	R_SelectTexture(GL_TEXTURE0);
}

#pragma endregion
