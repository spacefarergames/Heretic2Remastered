//
// gl3_Surface.c
//
// OpenGL 3.3 Core Profile world/brush surface rendering.
// Based on gl1_Surface.c -- Copyright 1998 Raven Software
//

#include "gl3_Surface.h"
#include "gl3_FlexModel.h"
#include "gl3_Image.h"
#include "gl3_Light.h"
#include "gl3_Lightmap.h"
#include "gl3_Misc.h"
#include "gl3_Shaders.h"
#include "gl3_Sky.h"
#include "gl3_Warp.h"
#include "Vector.h"

//mxd. Reconstructed data type. Original name unknown.
typedef struct
{
	union
	{
		entity_t* entity;
		msurface_t* surface;
	};
	float depth;
} AlphaSurfaceSortInfo_t;

int c_visible_lightmaps;
int c_visible_textures;

static int r_visframecount; // Bumped when going to a new PVS.

static vec3_t modelorg; // Relative to viewpoint.

static msurface_t* r_alpha_surfaces;

#pragma region ========================== ALPHA SURFACES RENDERING ==========================

static int AlphaSurfComp(const AlphaSurfaceSortInfo_t* info1, const AlphaSurfaceSortInfo_t* info2)
{
	return (int)((info2->depth - info1->depth) * 1000.0f);
}

static void R_DrawAlphaEntity(entity_t* ent)
{
	if (!(int)r_drawentities->value)
		return;

	if (ent->model == NULL)
	{
		ri.Con_Printf(PRINT_DEVELOPER, "Attempt to draw NULL alpha model\n");
		R_DrawNullModel(ent);

		return;
	}

	const model_t* mdl = *ent->model;

	if (mdl == NULL)
	{
		R_DrawNullModel(ent);
		return;
	}

	switch (mdl->type)
	{
		case mod_bad:
			ri.Con_Printf(PRINT_ALL, "WARNING: currentmodel->type == 0; reload the map\n");
			break;

		case mod_brush:
			R_DrawBrushModel(ent);
			break;

		case mod_sprite:
			R_DrawSpriteModel(ent);
			break;

		case mod_fmdl:
			R_DrawFlexModel(ent);
			break;

		default:
			ri.Sys_Error(ERR_DROP, "Bad modeltype");
			break;
	}
}

static void R_DrawAlphaSurface(const msurface_t* fa)
{
	// GL3: use shader3D for alpha surfaces.
	R_BindImage(fa->texinfo->image);
	c_brush_polys += 1;

	float alpha;
	if (fa->texinfo->flags & SURF_TRANS33)
		alpha = gl_trans33->value;
	else if (fa->texinfo->flags & SURF_TRANS66)
		alpha = gl_trans66->value;
	else
		alpha = 1.0f;

	glEnable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

	const float ii = gl_state.inverse_intensity;

	if (fa->flags & SURF_DRAWTURB)
	{
		GL3_Set3DColor(ii, ii, ii, alpha);
		R_EmitWaterPolys(fa, fa->flags & SURF_UNDULATE);
		GL3_Set3DColor(1.0f, 1.0f, 1.0f, 1.0f);
	}
	else
	{
		// Build 9-float vertex data from glpoly_t for shader3D.
		const glpoly_t* p = fa->polys;
		if (p != NULL)
		{
			float vbuf[64 * 9];
			const float* v = p->verts[0];
			for (int i = 0; i < p->numverts && i < 64; i++, v += VERTEXSIZE)
			{
				float* dest = &vbuf[i * 9];
				dest[0] = v[0]; dest[1] = v[1]; dest[2] = v[2]; // pos
				dest[3] = v[3]; dest[4] = v[4]; // tc
				dest[5] = ii; dest[6] = ii; dest[7] = ii; dest[8] = alpha; // col
			}

			GL3_Draw3DPoly(GL_TRIANGLE_FAN, vbuf, p->numverts);
		}
	}

	glDisable(GL_BLEND);
}

void R_SortAndDrawAlphaSurfaces(void)
{
#define MAX_ALPHA_SURFACES 512

	AlphaSurfaceSortInfo_t sorted_ents[MAX_ALPHA_ENTITIES + 1];
	AlphaSurfaceSortInfo_t sorted_surfs[MAX_ALPHA_SURFACES + 1];

	// Add alpha entities to array...
	AlphaSurfaceSortInfo_t* info = &sorted_ents[0];
	for (int i = 0; i < r_newrefdef.num_alpha_entities; i++, info++)
	{
		entity_t* ent = r_newrefdef.alpha_entities[i];

		info->entity = ent;
		info->depth = ent->depth;
	}

	VectorScale(r_origin, -1.0f, modelorg);

	// Initialize last entity entry...
	info = &sorted_ents[r_newrefdef.num_alpha_entities];
	info->entity = NULL;
	info->depth = -100000.0f;

	// Add alpha surfaces to array.
	int num_surfaces;
	msurface_t* surf = r_alpha_surfaces;
	info = &sorted_surfs[0];
	for (num_surfaces = 0; surf != NULL; num_surfaces++, surf = surf->texturechain, info++)
	{
		info->surface = surf;
		info->depth = -100000.0f;

		for (int i = 0; i < surf->numedges; i++)
		{
			const int lindex = r_worldmodel->surfedges[surf->firstedge + i];
			float* vec;

			if (lindex > 0)
			{
				const medge_t* edge = &r_worldmodel->edges[lindex];
				vec = r_worldmodel->vertexes[edge->v[0]].position;
			}
			else
			{
				const medge_t* edge = &r_worldmodel->edges[-lindex];
				vec = r_worldmodel->vertexes[edge->v[1]].position;
			}

			vec3_t diff;
			VectorSubtract(vec, r_origin, diff);

			vec3_t screen_pos;
			R_TransformVector(diff, screen_pos);

			info->depth = max(info->depth, screen_pos[2]);
		}

		if (num_surfaces >= MAX_ALPHA_SURFACES)
		{
			ri.Con_Printf(PRINT_DEVELOPER, "Warning: attempting to draw too many alpha surfaces\n");
			break;
		}
	}

	// Initialize last surface entry...
	info = &sorted_surfs[num_surfaces];
	info->surface = NULL;
	info->depth = -100000.0f;

	// Sort surfaces...
	qsort(sorted_surfs, num_surfaces, sizeof(AlphaSurfaceSortInfo_t), (int (*)(const void*, const void*))AlphaSurfComp);

	const int num_elements = r_newrefdef.num_alpha_entities + num_surfaces;
	const AlphaSurfaceSortInfo_t* sorted_ent = &sorted_ents[0];
	const AlphaSurfaceSortInfo_t* sorted_surf = &sorted_surfs[0];

	// Draw them all.
	for (int i = 0; i < num_elements; i++)
	{
		if (sorted_surf->depth > sorted_ent->depth)
		{
			R_DrawAlphaSurface(sorted_surf->surface);
			sorted_surf++;
		}
		else
		{
			R_DrawAlphaEntity(sorted_ent->entity);
			sorted_ent++;
		}
	}

	r_alpha_surfaces = NULL;
}

#pragma endregion

#pragma region ========================== BRUSH MODELS RENDERING ==========================

// Returns the proper texture for a given time and base texture.
static image_t* R_TextureAnimation(const entity_t* ent, const mtexinfo_t* tex)
{
	if (tex->next != NULL)
	{
		int frame;

		if ((tex->flags & SURF_ANIMSPEED) && tex->image->num_frames > 0)
			frame = (int)((float)tex->image->num_frames * r_newrefdef.time);
		else if (ent != NULL)
			frame = ent->frame;
		else
			return tex->image;

		frame %= tex->numframes;

		while (frame-- > 0 && tex->next != NULL)
			tex = tex->next;
	}

	return tex->image;
}

// GL3: Draw a glpoly_t using shader3D (9 floats/vert: pos3+tc2+col4) with white color.
static void R_DrawGLPoly3D(const glpoly_t* p)
{
	float vbuf[64 * 9];
	const float* v = p->verts[0];
	for (int i = 0; i < p->numverts && i < 64; i++, v += VERTEXSIZE)
	{
		float* dest = &vbuf[i * 9];
		dest[0] = v[0]; dest[1] = v[1]; dest[2] = v[2]; // pos
		dest[3] = v[3]; dest[4] = v[4]; // tc
		dest[5] = 1.0f; dest[6] = 1.0f; dest[7] = 1.0f; dest[8] = 1.0f; // col
	}

	GL3_Draw3DPoly(GL_TRIANGLE_FAN, vbuf, p->numverts);
}

// GL3: Single-pass lightmapped surface rendering using shader3DLightmap.
static void R_RenderLightmappedPoly(const entity_t* ent, msurface_t* surf)
{
	static uint lightmap_pixels[LM_BLOCK_WIDTH * LM_BLOCK_HEIGHT];

	int map;
	int lmtex = surf->lightmaptexturenum;
	qboolean lightmap_updated = false;

	for (map = 0; map < MAXLIGHTMAPS && surf->styles[map] != 255; map++)
	{
		if (r_newrefdef.lightstyles[surf->styles[map]].white != surf->cached_light[map])
		{
			lightmap_updated = true;
			break;
		}
	}

	// Dynamic this frame or dynamic previously.
	qboolean is_dynamic = false;
	if (lightmap_updated || surf->dlightframe == r_framecount)
		is_dynamic = ((int)gl_dynamic->value && !(surf->texinfo->flags & SURF_FULLBRIGHT));

	if (is_dynamic)
	{
		const int smax = (surf->extents[0] >> 4) + 1;
		const int tmax = (surf->extents[1] >> 4) + 1;

		R_BuildLightMap(surf, (byte*)lightmap_pixels, smax * 4);

		if ((surf->styles[map] >= 32 || surf->styles[map] == 0) && surf->dlightframe != r_framecount)
		{
			R_SetCacheState(surf);
			R_MBind(GL_TEXTURE1, gl3state.lightmap_textures[surf->lightmaptexturenum]);
			lmtex = surf->lightmaptexturenum;
		}
		else
		{
			R_MBind(GL_TEXTURE1, gl3state.lightmap_textures[0]);
			lmtex = 0;
		}

		glTexSubImage2D(GL_TEXTURE_2D, 0, surf->light_s, surf->light_t, smax, tmax, GL_LIGHTMAP_FORMAT, GL_UNSIGNED_BYTE, lightmap_pixels);
	}

	c_brush_polys++;

	// Bind diffuse to unit 0.
	R_MBindImage(GL_TEXTURE0, R_TextureAnimation(ent, surf->texinfo));
	// Bind lightmap to unit 1.
	R_MBind(GL_TEXTURE1, gl3state.lightmap_textures[lmtex]);

	// Draw with shader3DLightmap.
	GL3_SetLMColor(1.0f, 1.0f, 1.0f, 1.0f);
	for (glpoly_t* p = surf->polys; p != NULL; p = p->chain)
		GL3_DrawLMPoly(p->verts[0], p->numverts);

	// Restore TMU0 as the active texture unit so subsequent single-texture
	// rendering (sky, models, HUD, etc.) binds diffuse textures to unit 0.
	R_SelectTexture(GL_TEXTURE0);
}

static void R_RenderBrushPoly(const entity_t* ent, msurface_t* fa)
{
	c_brush_polys++;

	R_BindImage(R_TextureAnimation(ent, fa->texinfo));

	if ((int)cl_camera_under_surface->value)
	{
		R_EmitUnderwaterPolys(fa);
		return;
	}

	if ((int)quake_amount->value)
	{
		R_EmitQuakeFloorPolys(fa);
		return;
	}

	if (fa->flags & SURF_DRAWTURB)
	{
		// Warp texture, no lightmaps.
		const float ii = gl_state.inverse_intensity;
		GL3_Set3DColor(ii, ii, ii, 1.0f);
		R_EmitWaterPolys(fa, fa->flags & SURF_UNDULATE);
		GL3_Set3DColor(1.0f, 1.0f, 1.0f, 1.0f);

		return;
	}

	// Non-turb, non-lightmapped surface - draw with shader3D.
	R_DrawGLPoly3D(fa->polys);

	int map;
	qboolean is_dynamic = false;

	// Check for lightmap modification.
	for (map = 0; map < MAXLIGHTMAPS && fa->styles[map] != 255; map++)
	{
		if (r_newrefdef.lightstyles[fa->styles[map]].white != fa->cached_light[map])
		{
			is_dynamic = true;
			break;
		}
	}

	// Dynamic this frame or dynamic previously.
	if (fa->dlightframe == r_framecount || is_dynamic)
	{
		if ((int)gl_dynamic->value && !(fa->texinfo->flags & SURF_FULLBRIGHT))
		{
			if ((fa->styles[map] >= 32 || fa->styles[map] == 0) && fa->dlightframe != r_framecount)
			{
				uint temp[34 * 34];
				const int smax = (fa->extents[0] >> 4) + 1;
				const int tmax = (fa->extents[1] >> 4) + 1;

				R_BuildLightMap(fa, (byte*)temp, smax * 4);
				R_SetCacheState(fa);
				R_Bind(gl3state.lightmap_textures[fa->lightmaptexturenum]);

				glTexSubImage2D(GL_TEXTURE_2D, 0, fa->light_s, fa->light_t, smax, tmax, GL_LIGHTMAP_FORMAT, GL_UNSIGNED_BYTE, temp);

				fa->lightmapchain = gl_lms.lightmap_surfaces[fa->lightmaptexturenum];
				gl_lms.lightmap_surfaces[fa->lightmaptexturenum] = fa;
			}
			else
			{
				fa->lightmapchain = gl_lms.lightmap_surfaces[0];
				gl_lms.lightmap_surfaces[0] = fa;
			}

			return;
		}
	}

	if (!(fa->texinfo->flags & SURF_TALL_WALL))
	{
		fa->lightmapchain = gl_lms.lightmap_surfaces[fa->lightmaptexturenum];
		gl_lms.lightmap_surfaces[fa->lightmaptexturenum] = fa;
	}
	else if (gl_lms.tallwall_lightmaptexturenum < MAX_TALLWALL_LIGHTMAPS)
	{
		gl_lms.tallwall_lightmap_surfaces[gl_lms.tallwall_lightmaptexturenum] = fa;
		gl_lms.tallwall_lightmaptexturenum++;
	}
	else
	{
		ri.Con_Printf(PRINT_ALL, "WARNING: too many tall wall surfaces!");
	}
}

static void R_DrawTextureChains(const entity_t* ent)
{
	c_visible_textures = 0;

	// GL3: Process multitexture chain (lightmapped surfaces, single-pass via shader3DLightmap).
	{
		image_t* image = &gltextures[0];
		for (int i = 0; i < numgltextures; i++, image++)
		{
			if (image->registration_sequence == 0 || image->multitexturechain == NULL)
				continue;

			c_visible_textures++;

			for (msurface_t* s = image->multitexturechain; s != NULL; s = s->texturechain)
				R_RenderLightmappedPoly(ent, s);

			image->multitexturechain = NULL;
		}
	}

	// Process standard texture chain (warp, fullbright, drawflat surfaces).
	// Render non-turb surfaces first.
	{
		image_t* image = &gltextures[0];
		for (int i = 0; i < numgltextures; i++, image++)
		{
			if (!image->registration_sequence || image->texturechain == NULL)
				continue;

			c_visible_textures++;

			for (msurface_t* s = image->texturechain; s != NULL; s = s->texturechain)
				if (!(s->flags & SURF_DRAWTURB))
					R_RenderBrushPoly(ent, s);
		}
	}

	// Render warping (water) surfaces (no lightmaps).
	{
		image_t* image = &gltextures[0];
		for (int i = 0; i < numgltextures; i++, image++)
		{
			if (!image->registration_sequence || image->texturechain == NULL)
				continue;

			for (msurface_t* s = image->texturechain; s != NULL; s = s->texturechain)
				if (s->flags & SURF_DRAWTURB)
					R_RenderBrushPoly(ent, s);

			image->texturechain = NULL;
		}
	}
}

static qboolean R_CullBox(const vec3_t mins, const vec3_t maxs)
{
	if (!(int)r_nocull->value)
	{
		for (int i = 0; i < 4; i++)
			if (BoxOnPlaneSide(mins, maxs, &frustum[i]) == 2)
				return true;
	}

	return false;
}

static void R_DrawInlineBModel(const entity_t* ent)
{
#define BACKFACE_EPSILON 0.01f

	const model_t* mdl = *ent->model;

	// Calculate dynamic lighting for bmodel.
	if (!(int)gl_flashblend->value)
	{
		dlight_t* lt = r_newrefdef.dlights;
		for (int k = 0; k < r_newrefdef.num_dlights; k++, lt++)
			R_MarkLights(lt, 1 << k, mdl->nodes + mdl->firstnode);
	}

	msurface_t* psurf = &mdl->surfaces[mdl->firstmodelsurface];

	if (ent->flags & RF_TRANS_ANY)
	{
		glEnable(GL_BLEND);
		GL3_SetLMColor(1.0f, 1.0f, 1.0f, 0.25f);
	}

	// Draw texture.
	for (int i = 0; i < mdl->nummodelsurfaces; i++, psurf++)
	{
		// Find which side of the node we are on.
		const cplane_t* pplane = psurf->plane;
		const float dot = DotProduct(modelorg, pplane->normal) - pplane->dist;

		// Draw the polygon.
		if (((psurf->flags & SURF_PLANEBACK) && dot < -BACKFACE_EPSILON) ||
			(!(psurf->flags & SURF_PLANEBACK) && dot > BACKFACE_EPSILON))
		{
			if (psurf->texinfo->flags & (SURF_TRANS33 | SURF_TRANS66))
			{
				// Add to the translucent chain.
				psurf->texturechain = r_alpha_surfaces;
				r_alpha_surfaces = psurf;
			}
			else if (!(psurf->flags & SURF_DRAWTURB) && !(int)r_fullbright->value && !(int)gl_drawflat->value)
			{
				R_RenderLightmappedPoly(ent, psurf);
			}
			else
			{
				R_RenderBrushPoly(ent, psurf);
			}
		}
	}

	if (ent->flags & RF_TRANS_ANY)
	{
		glDisable(GL_BLEND);
		GL3_SetLMColor(1.0f, 1.0f, 1.0f, 1.0f);
	}
}

void R_DrawBrushModel(entity_t* ent)
{
	const model_t* mdl = *ent->model;

	if (mdl->nummodelsurfaces == 0)
		return;

	gl_state.currenttextures[0] = -1;
	gl_state.currenttextures[1] = -1;

	vec3_t mins;
	vec3_t maxs;
	qboolean rotated;

	if (ent->angles[0] != 0.0f || ent->angles[1] != 0.0f || ent->angles[2] != 0.0f)
	{
		for (int i = 0; i < 3; i++)
		{
			mins[i] = ent->origin[i] - mdl->radius;
			maxs[i] = ent->origin[i] + mdl->radius;
		}

		rotated = true;
	}
	else
	{
		VectorAdd(ent->origin, mdl->mins, mins);
		VectorAdd(ent->origin, mdl->maxs, maxs);

		rotated = false;
	}

	if (R_CullBox(mins, maxs))
		return;

	memset((void*)gl_lms.lightmap_surfaces, 0, sizeof(gl_lms.lightmap_surfaces));
	VectorSubtract(r_newrefdef.vieworg, ent->origin, modelorg);

	if (rotated)
	{
		vec3_t angles;
		VectorScale(ent->angles, RAD_TO_ANGLE, angles);

		const vec3_t temp = VEC3_INIT(modelorg);

		vec3_t forward;
		vec3_t right;
		vec3_t up;
		AngleVectors(angles, forward, right, up);

		modelorg[0] = DotProduct(temp, forward);
		modelorg[1] = -DotProduct(temp, right);
		modelorg[2] = DotProduct(temp, up);
	}

	// GL3: set entity modelview matrix instead of glPushMatrix + glRotateForEntity.
	ent->angles[0] *= -1.0f; // stupid quake bug.
	ent->angles[2] *= -1.0f; // stupid quake bug.
	R_RotateForEntity(ent);
	ent->angles[0] *= -1.0f; // stupid quake bug.
	ent->angles[2] *= -1.0f; // stupid quake bug.

	R_DrawInlineBModel(ent);

	// GL3: restore world modelview matrix.
	GL3_UpdateModelview3D(r_world_matrix);
	GL3_UpdateModelviewLM(r_world_matrix);
}

#pragma endregion

#pragma region ========================== WORLD MODEL RENDERING ==========================

static void R_RecursiveWorldNode(const entity_t* ent, mnode_t* node)
{
	if (node->contents == CONTENTS_SOLID || node->visframe != r_visframecount || R_CullBox(node->minmaxs, node->minmaxs + 3))
		return;

	// If a leaf node, draw stuff.
	if (node->contents != -1)
	{
		const mleaf_t* pleaf = (mleaf_t*)node;

		// Check for door connected areas.
		if (r_newrefdef.areabits != NULL && !(r_newrefdef.areabits[pleaf->area >> 3] & (1 << (pleaf->area & 7))))
			return; // Not visible.

		msurface_t** mark = pleaf->firstmarksurface;
		for (int i = pleaf->nummarksurfaces; i > 0; i--)
		{
			(*mark)->visframe = r_framecount;
			mark++;
		}

		return;
	}

	// Node is just a decision point, so go down the appropriate sides.

	// Find which side of the node we are on.
	const cplane_t* plane = node->plane;
	float dot;

	switch (plane->type)
	{
		case PLANE_X:
		case PLANE_Y:
		case PLANE_Z:
			dot = modelorg[plane->type] - plane->dist;
			break;

		default:
			dot = DotProduct(modelorg, plane->normal) - plane->dist;
			break;
	}

	const int side = ((dot >= 0.0f) ? 0 : 1);
	const int sidebit = ((dot >= 0.0f) ? 0 : SURF_PLANEBACK);

	// Recurse down the children, front side first.
	R_RecursiveWorldNode(ent, node->children[side]);

	// Draw stuff.
	msurface_t* surf = &r_worldmodel->surfaces[node->firstsurface];
	for (int c = node->numsurfaces; c > 0; c--, surf++)
	{
		if (surf->visframe != r_framecount || (surf->flags & SURF_PLANEBACK) != sidebit)
			continue; // Wrong frame or side.

		if (surf->texinfo->flags & SURF_SKY)
		{
			// Just adds to visible sky bounds.
			R_AddSkySurface(surf);
		}
		else if (surf->texinfo->flags & (SURF_TRANS33 | SURF_TRANS66))
		{
			// Add to the translucent texture chain.
			surf->texturechain = r_alpha_surfaces;
			r_alpha_surfaces = surf;
		}
		else if (!(surf->flags & SURF_DRAWTURB) && !(surf->flags & SURF_TALL_WALL) && !(int)r_fullbright->value && !(int)gl_drawflat->value)
		{
			// GL3: always add to multitexture chain for single-pass lightmapped rendering.
			image_t* image = R_TextureAnimation(ent, surf->texinfo);
			surf->texturechain = image->multitexturechain;
			image->multitexturechain = surf;
		}
		else
		{
			// The polygon is visible, so add it to the sorted texture chain.
			image_t* image = R_TextureAnimation(ent, surf->texinfo);
			surf->texturechain = image->texturechain;
			image->texturechain = surf;
		}
	}

	// Recurse down the back side.
	R_RecursiveWorldNode(ent, node->children[!side]);
}

void R_DrawWorld(void)
{
	if (!(int)r_drawworld->value || (r_newrefdef.rdflags & RDF_NOWORLDMODEL))
		return;

	VectorCopy(r_newrefdef.vieworg, modelorg);

	// Auto cycle the world frame for texture animation.
	const entity_t ent = { .frame = (int)(r_newrefdef.time * 2.0f) };

	gl_state.currenttextures[0] = -1;
	gl_state.currenttextures[1] = -1;

	memset((void*)gl_lms.lightmap_surfaces, 0, sizeof(gl_lms.lightmap_surfaces));
	gl_lms.tallwall_lightmaptexturenum = 0;

	R_ClearSkyBox();

	R_RecursiveWorldNode(&ent, r_worldmodel->nodes);

	R_DrawTextureChains(&ent);

	R_DrawSkyBox();
}

// Mark the leaves and nodes that are in the PVS for the current cluster.
void R_MarkLeaves(void)
{
	static byte fatvis[MAX_MAP_LEAFS / 8];

	if (r_oldviewcluster == r_viewcluster && r_oldviewcluster2 == r_viewcluster2 && !(int)r_novis->value && r_viewcluster != -1)
		return;

	// Development aid to let you run around and see exactly where the pvs ends.
	if ((int)gl_lockpvs->value)
		return;

	r_visframecount++;
	r_oldviewcluster = r_viewcluster;
	r_oldviewcluster2 = r_viewcluster2;

	if ((int)r_novis->value || r_viewcluster == -1 || r_worldmodel->vis == NULL)
	{
		// Mark everything.
		for (int i = 0; i < r_worldmodel->numleafs; i++)
			r_worldmodel->leafs[i].visframe = r_visframecount;

		for (int i = 0; i < r_worldmodel->numnodes; i++)
			r_worldmodel->nodes[i].visframe = r_visframecount;

		return;
	}

	byte* vis = Mod_ClusterPVS(r_viewcluster, r_worldmodel);

	// May have to combine two clusters because of solid water boundaries.
	if (r_viewcluster2 != r_viewcluster)
	{
		memcpy(fatvis, vis, (r_worldmodel->numleafs + 7) / 8);
		vis = Mod_ClusterPVS(r_viewcluster2, r_worldmodel);

		const int c = (r_worldmodel->numleafs + 31) / 32;
		for (int i = 0; i < c; i++)
			((int*)fatvis)[i] |= ((int*)vis)[i];

		vis = fatvis;
	}

	mleaf_t* leaf = &r_worldmodel->leafs[0];
	for (int i = 0; i < r_worldmodel->numleafs; i++, leaf++)
	{
		const int cluster = leaf->cluster;
		if (cluster == -1)
			continue;

		if (vis[cluster >> 3] & 1 << (cluster & 7))
		{
			mnode_t* node = (mnode_t*)leaf;
			do
			{
				if (node->visframe == r_visframecount)
					break;

				node->visframe = r_visframecount;
				node = node->parent;
			} while (node);
		}
	}
}

#pragma endregion
