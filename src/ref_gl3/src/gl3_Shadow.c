//
// gl3_Shadow.c
//
// Dynamic planar shadow: BSP floor trace + model-silhouette projection.
// Each entity's interpolated flex-model geometry is re-rendered with a combined
// (r_world_matrix x M_flat x M_entity) modelview that squishes every vertex
// onto the traced floor plane, producing a true model-silhouette shadow.
//

#include "gl3_Shadow.h"
#include "gl3_FlexModel.h"
#include "gl3_Misc.h"
#include "gl3_Shaders.h"
#include "Skeletons/r_SkeletonLerp.h"
#include "Vector.h"
#include <math.h>

#define SHADOW_TRACE_DIST   256.0f
#define SHADOW_MAX_ALPHA    0.65f

// ============================================================
// BSP downward trace.
// ============================================================

static qboolean s_shadow_hit;
static vec3_t   s_shadow_hitpos;
static vec3_t   s_shadow_hitnormal;

static void R_RecursiveShadowTrace(const mnode_t* node, const vec3_t start, const vec3_t end)
{
	if (s_shadow_hit)
		return;

	// Leaf node: no surfaces stored here in H2's rendering BSP.
	if (node->contents != -1)
		return;

	const cplane_t* plane = node->plane;
	const float front = DotProduct(start, plane->normal) - plane->dist;
	const float back  = DotProduct(end,   plane->normal) - plane->dist;
	const int   side  = (front < 0.0f);

	// Both endpoints on the same side: only recurse into that child.
	if ((back < 0.0f) == side)
	{
		R_RecursiveShadowTrace(node->children[side], start, end);
		return;
	}

	// Segment crosses this plane: compute crossing point.
	const float frac = front / (front - back);
	vec3_t mid;
	VectorLerp(start, frac, end, mid);

	// Recurse the near (front) side first.
	R_RecursiveShadowTrace(node->children[side], start, mid);
	if (s_shadow_hit)
		return;

	// Check surfaces at this node for an upward-facing floor candidate.
	const msurface_t* surf = &r_worldmodel->surfaces[node->firstsurface];
	for (int i = 0; i < node->numsurfaces; i++, surf++)
	{
		if (surf->flags & (SURF_DRAWTURB | SURF_DRAWSKY | SURF_SKIPDRAW))
			continue;

		const cplane_t* sp = surf->plane;
		const float nz = (surf->flags & SURF_PLANEBACK) ? -sp->normal[2] : sp->normal[2];
		if (nz < 0.5f)
			continue;

		// Bounds check: verify the crossing point is inside the surface's ST extents.
		const mtexinfo_t* tex = surf->texinfo;
		if (tex == NULL)
			continue;

		const int s = (int)(DotProduct(mid, tex->vecs[0]) + tex->vecs[0][3]);
		const int t = (int)(DotProduct(mid, tex->vecs[1]) + tex->vecs[1][3]);
		if (s < surf->texturemins[0] || t < surf->texturemins[1])
			continue;

		const int ds = s - surf->texturemins[0];
		const int dt = t - surf->texturemins[1];
		if (ds > surf->extents[0] || dt > surf->extents[1])
			continue;

		// Compute the exact floor Z from the surface's own plane equation.
		// The ray is purely vertical (X,Y = mid[0], mid[1] = start[0], start[1]).
		// Plane: sp->normal · P = sp->dist  →  Pz = (dist - Nx·Px - Ny·Py) / Nz_geom
		const float geom_nz = sp->normal[2];
		if (fabsf(geom_nz) < 0.001f)
			continue;

		const float hit_z = (sp->dist - sp->normal[0] * mid[0] - sp->normal[1] * mid[1]) / geom_nz;

		// Reject if the floor is above the entity origin or below the trace range.
		if (hit_z > start[2] + 1.0f || hit_z < start[2] - SHADOW_TRACE_DIST)
			continue;

		s_shadow_hit = true;
		s_shadow_hitpos[0] = mid[0];
		s_shadow_hitpos[1] = mid[1];
		s_shadow_hitpos[2] = hit_z;

		if (surf->flags & SURF_PLANEBACK)
		{
			s_shadow_hitnormal[0] = -sp->normal[0];
			s_shadow_hitnormal[1] = -sp->normal[1];
			s_shadow_hitnormal[2] = -sp->normal[2];
		}
		else
		{
			VectorCopy(sp->normal, s_shadow_hitnormal);
		}
		return;
	}

	// Recurse the far side.
	R_RecursiveShadowTrace(node->children[!side], mid, end);
}

static qboolean R_ShadowTrace(const vec3_t origin, vec3_t out_pos, vec3_t out_normal)
{
	if (r_worldmodel == NULL || r_worldmodel->nodes == NULL)
		return false;

	const vec3_t end = VEC3_INITA(origin, 0.0f, 0.0f, -SHADOW_TRACE_DIST);

	s_shadow_hit = false;
	R_RecursiveShadowTrace(r_worldmodel->nodes, origin, end);

	if (s_shadow_hit)
	{
		VectorCopy(s_shadow_hitpos,    out_pos);
		VectorCopy(s_shadow_hitnormal, out_normal);
	}

	return s_shadow_hit;
}

// ============================================================
// Per-entity shadow projection.
// ============================================================

static void R_DrawEntityShadow(entity_t* ent)
{
	if (ent->model == NULL)
		return;

	const model_t* mdl = *ent->model;
	if (mdl == NULL || mdl->type != mod_fmdl)
		return;

	if (ent->flags & RF_TRANS_ANY)
		return;

	const fmdl_t* fmdl = (const fmdl_t*)mdl->extradata;
	if (fmdl == NULL || fmdl->mesh_nodes == NULL || fmdl->glcmds == NULL)
		return;

	vec3_t hitpos, hitnormal;
	if (!R_ShadowTrace(ent->origin, hitpos, hitnormal))
		return;

	// Height above floor: used to fade and reject out-of-range shadows.
	const float height = ent->origin[2] - hitpos[2];
	if (height < 0.0f || height >= SHADOW_TRACE_DIST)
		return;

	const float alpha = SHADOW_MAX_ALPHA * (1.0f - height / SHADOW_TRACE_DIST);
	if (alpha < 0.01f)
		return;

	// Populate s_lerped[] with interpolated model-space vertex positions.
	FrameLerp(fmdl, ent);

	// --- Build M_entity: entity TRS (same order as R_RotateForEntity) ---
	float M_entity[16];
	R_Mat4x4_Identity(M_entity);
	R_Mat4x4_Translate(M_entity, ent->origin[0], ent->origin[1], ent->origin[2]);
	R_Mat4x4_Rotate(M_entity,  ent->angles[1] * RAD_TO_ANGLE, 0.0f, 0.0f, 1.0f);
	R_Mat4x4_Rotate(M_entity, -ent->angles[0] * RAD_TO_ANGLE, 0.0f, 1.0f, 0.0f);
	R_Mat4x4_Rotate(M_entity, -ent->angles[2] * RAD_TO_ANGLE, 1.0f, 0.0f, 0.0f);

	// --- Build M_flat: planar projection matrix ---
	// Light direction L = (0,0,-1) (straight down). Floor plane: hitnormal · P = d.
	// Shadow of world-space point P: P' = P + ((d - N·P) / Nz) * L
	//   P'x = Px,  P'y = Py,  P'z = (d - Nx·Px - Ny·Py) / Nz
	// Column-major 4x4 layout (m[col*4 + row]):
	//   col 0: [1, 0, -Nx/Nz, 0]
	//   col 1: [0, 1, -Ny/Nz, 0]
	//   col 2: [0, 0,  0,     0]
	//   col 3: [0, 0,  d/Nz,  1]
	const float Nx = hitnormal[0], Ny = hitnormal[1], Nz = hitnormal[2];
	const float d  = DotProduct(hitnormal, hitpos);
	const float inv_nz = 1.0f / Nz;  // Nz >= 0.5 guaranteed by trace

	const float M_flat[16] = {
		1.0f,  0.0f, -Nx * inv_nz,  0.0f,   // col 0
		0.0f,  1.0f, -Ny * inv_nz,  0.0f,   // col 1
		0.0f,  0.0f,  0.0f,         0.0f,   // col 2
		0.0f,  0.0f,  d  * inv_nz,  1.0f,   // col 3
	};

	// shadow_mv = r_world_matrix × M_flat × M_entity
	float M_fe[16], shadow_mv[16];
	R_Mat4x4_Mul(M_fe,      M_flat,         M_entity);
	R_Mat4x4_Mul(shadow_mv, r_world_matrix, M_fe);

	// Override the shader3D modelview with the shadow projection matrix.
	// GL3_Draw3DPoly will call GL3_UseShader(shader3D) but uniforms persist.
	GL3_UseShader(gl3state.shader3D);
	glUniformMatrix4fv(gl3state.uni3D_modelview, 1, GL_FALSE, shadow_mv);

	// Render each mesh node's geometry flattened onto the floor plane.
	// uColor = (0,0,0,1) makes the shadow black; vertex alpha carries height fade.
	static float vbuf[MAX_FM_VERTS * 9];

	for (int i = 0; i < fmdl->header.num_mesh_nodes; i++)
	{
		if (ent->fmnodeinfo != NULL && (ent->fmnodeinfo[i].flags & FMNI_NO_DRAW))
			continue;

		int* order = &fmdl->glcmds[fmdl->mesh_nodes[i].start_glcmds];
		while (true)
		{
			int num_verts = *order++;
			if (num_verts == 0)
				break;

			GLenum prim;
			if (num_verts < 0)
			{
				num_verts = -num_verts;
				prim = GL_TRIANGLE_FAN;
			}
			else
			{
				prim = GL_TRIANGLE_STRIP;
			}

			for (int c = 0; c < num_verts; c++)
			{
				const int index_xyz = order[2];
				float* v = &vbuf[c * 9];

				// Model-space position from lerped vertex array.
				v[0] = s_lerped[index_xyz][0];
				v[1] = s_lerped[index_xyz][1];
				v[2] = s_lerped[index_xyz][2];

				// TC unused (white texture sampled; uColor kills RGB anyway).
				v[3] = 0.0f;
				v[4] = 0.0f;

				// Vertex RGB = white so component-wise multiply with uColor=(0,0,0,1)
				// yields (0, 0, 0, alpha) — black shadow at the desired opacity.
				v[5] = 1.0f;
				v[6] = 1.0f;
				v[7] = 1.0f;
				v[8] = alpha;

				order += 3;
			}

			GL3_Draw3DPoly(prim, vbuf, num_verts);
		}
	}
}

// ============================================================
// Public API.
// ============================================================

void R_InitShadows(void)
{
	ri.Con_Printf(PRINT_ALL, "GL3 dynamic shadows initialized.\n");
}

void R_ShutdownShadows(void)
{
}

void R_DrawEntityShadows(void)
{
	if (r_worldmodel == NULL)
		return;

	// Bind 1x1 white texture to TMU0 so shader3D sampling always returns white.
	// With uColor = (0,0,0,1), the result is (0,0,0,vertex_alpha) = black shadow.
	glActiveTexture(GL_TEXTURE0);
	glBindTexture(GL_TEXTURE_2D, gl3state.whiteTexture);
	gl_state.currenttmu = 0;
	gl_state.currenttextures[0] = (int)gl3state.whiteTexture;

	GL3_UseShader(gl3state.shader3D);
	glUniform1i(gl3state.uni3D_numDlights, 0);
	glUniform4f(gl3state.uni3D_color, 0.0f, 0.0f, 0.0f, 1.0f);

	// Depth-read, no depth write, alpha blend, polygon offset to prevent z-fighting.
	glEnable(GL_DEPTH_TEST);
	glDepthMask(GL_FALSE);
	glEnable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	glDisable(GL_CULL_FACE);
	glEnable(GL_POLYGON_OFFSET_FILL);
	glPolygonOffset(-1.0f, -2.0f);

	for (int i = 0; i < r_newrefdef.num_entities; i++)
		R_DrawEntityShadow(r_newrefdef.entities[i]);

	// Restore render state and reset uniforms to defaults.
	glDisable(GL_POLYGON_OFFSET_FILL);
	glPolygonOffset(0.0f, 0.0f);
	glDepthMask(GL_TRUE);
	glDisable(GL_BLEND);
	glEnable(GL_CULL_FACE);

	GL3_UseShader(gl3state.shader3D);
	glUniform4f(gl3state.uni3D_color, 1.0f, 1.0f, 1.0f, 1.0f);
	GL3_UpdateModelview3D(r_world_matrix);
}
