//
// gl3_Shadow.c
//
// Dynamic planar shadow: BSP trace + model-silhouette projection.
// Each entity's interpolated flex-model geometry is re-rendered with a combined
// (r_world_matrix x M_shadow x M_entity) modelview that squishes every vertex
// onto the traced surface plane, producing a true model-silhouette shadow.
//
// Features:
//   - Floor shadows: downward trace, straight-down projection (L = 0,0,-1).
//   - Per-entity stencil overlap prevention (no triangle double-darkening).
//

#include "gl3_Shadow.h"
#include "gl3_FlexModel.h"
#include "gl3_Misc.h"
#include "gl3_Shaders.h"
#include "Skeletons/r_SkeletonLerp.h"
#include "Vector.h"
#include <math.h>

#define SHADOW_TRACE_DIST       256.0f
#define SHADOW_MAX_ALPHA        0.65f

// ============================================================
// Generalized shadow projection matrix.
// ============================================================

// Builds a 4x4 column-major planar projection matrix that projects world-space
// points along direction L onto the plane defined by (N, d) where N·P = d.
// Returns false if L is nearly parallel to the plane (degenerate).
static qboolean BuildShadowMatrix(const vec3_t N, const float d, const vec3_t L, float out[16])
{
	const float ndl = DotProduct(N, L);
	if (fabsf(ndl) < 0.001f)
		return false;

	const float inv = 1.0f / ndl;

	// Column-major: out[col*4 + row]
	out[0]  = 1.0f - L[0] * N[0] * inv;
	out[1]  =      - L[1] * N[0] * inv;
	out[2]  =      - L[2] * N[0] * inv;
	out[3]  = 0.0f;

	out[4]  =      - L[0] * N[1] * inv;
	out[5]  = 1.0f - L[1] * N[1] * inv;
	out[6]  =      - L[2] * N[1] * inv;
	out[7]  = 0.0f;

	out[8]  =      - L[0] * N[2] * inv;
	out[9]  =      - L[1] * N[2] * inv;
	out[10] = 1.0f - L[2] * N[2] * inv;
	out[11] = 0.0f;

	out[12] = L[0] * d * inv;
	out[13] = L[1] * d * inv;
	out[14] = L[2] * d * inv;
	out[15] = 1.0f;

	return true;
}

// ============================================================
// BSP shadow trace (parameterized for floor and wall).
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

	// Check surfaces at this node.
	const msurface_t* surf = &r_worldmodel->surfaces[node->firstsurface];
	for (int i = 0; i < node->numsurfaces; i++, surf++)
	{
		if (surf->flags & (SURF_DRAWTURB | SURF_DRAWSKY | SURF_SKIPDRAW))
			continue;

		const cplane_t* sp = surf->plane;

		// Compute geometric normal (accounting for SURF_PLANEBACK).
		vec3_t geom_n;
		if (surf->flags & SURF_PLANEBACK)
			VectorNegate(sp->normal, geom_n);
		else
			VectorCopy(sp->normal, geom_n);

		// Floor filter: geometric normal must point mostly upward.
		if (geom_n[2] < 0.5f)
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

		// Compute exact floor Z from the surface's plane equation.
		const float geom_nz = sp->normal[2];
		if (fabsf(geom_nz) < 0.001f)
			continue;

		const float hit_z = (sp->dist - sp->normal[0] * mid[0] - sp->normal[1] * mid[1]) / geom_nz;

		// Reject if the floor is above the entity origin or below the trace range.
		if (hit_z > start[2] + 1.0f || hit_z < start[2] - SHADOW_TRACE_DIST)
			continue;

		VectorSet(s_shadow_hitpos, mid[0], mid[1], hit_z);

		s_shadow_hit = true;
		VectorCopy(geom_n, s_shadow_hitnormal);
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

// Render the entity's mesh projected onto a single surface plane.
static void R_ProjectEntityShadow(const entity_t* ent, const fmdl_t* fmdl,
	const float M_entity[16], const vec3_t hit_normal, const vec3_t hit_pos,
	const vec3_t light_dir, const float alpha)
{
	float M_shadow[16];
	const float d = DotProduct(hit_normal, hit_pos);

	if (!BuildShadowMatrix(hit_normal, d, light_dir, M_shadow))
		return;

	// shadow_mv = r_world_matrix × M_shadow × M_entity
	float M_se[16], shadow_mv[16];
	R_Mat4x4_Mul(M_se,      M_shadow,       M_entity);
	R_Mat4x4_Mul(shadow_mv, r_world_matrix, M_se);

	GL3_UseShader(gl3state.shader3D);
	glUniformMatrix4fv(gl3state.uni3D_modelview, 1, GL_FALSE, shadow_mv);

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

				v[0] = s_lerped[index_xyz][0];
				v[1] = s_lerped[index_xyz][1];
				v[2] = s_lerped[index_xyz][2];

				v[3] = 0.0f;
				v[4] = 0.0f;

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

	// Populate s_lerped[] with interpolated model-space vertex positions.
	FrameLerp(fmdl, ent);

	// --- Build M_entity: entity TRS (same order as R_RotateForEntity) ---
	float M_entity[16];
	R_Mat4x4_Identity(M_entity);
	R_Mat4x4_Translate(M_entity, ent->origin[0], ent->origin[1], ent->origin[2]);
	R_Mat4x4_Rotate(M_entity,  ent->angles[1] * RAD_TO_ANGLE, 0.0f, 0.0f, 1.0f);
	R_Mat4x4_Rotate(M_entity, -ent->angles[0] * RAD_TO_ANGLE, 0.0f, 1.0f, 0.0f);
	R_Mat4x4_Rotate(M_entity, -ent->angles[2] * RAD_TO_ANGLE, 1.0f, 0.0f, 0.0f);

	// --- Floor shadow (project straight down) ---
	vec3_t hitpos, hitnormal;
	if (R_ShadowTrace(ent->origin, hitpos, hitnormal))
	{
		const float height = ent->origin[2] - hitpos[2];
		if (height >= 0.0f && height < SHADOW_TRACE_DIST)
		{
			const float alpha = SHADOW_MAX_ALPHA * (1.0f - height / SHADOW_TRACE_DIST);
			if (alpha >= 0.01f)
			{
				static const vec3_t light_down = { 0.0f, 0.0f, -1.0f };
				R_ProjectEntityShadow(ent, fmdl, M_entity, hitnormal, hitpos, light_down, alpha);
			}
		}
	}
}

// ============================================================
// Public API.
// ============================================================

void R_InitShadows(void)
{
	ri.Con_Printf(PRINT_ALL, "GL3 dynamic shadows initialized (stencil).\n");
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

	// Enable stencil test: per-entity overlap prevention.
	// Each entity uses a unique stencil ref (1..255). A pixel is only written if
	// its stencil value differs from the current ref, preventing overlapping
	// triangles of the same entity from double-darkening.
	glEnable(GL_STENCIL_TEST);
	glStencilMask(0xFF);

	const int num_ents = min(r_newrefdef.num_entities, 255);
	for (int i = 0; i < num_ents; i++)
	{
		const int stencil_ref = i + 1;
		glStencilFunc(GL_NOTEQUAL, stencil_ref, 0xFF);
		glStencilOp(GL_KEEP, GL_KEEP, GL_REPLACE);

		R_DrawEntityShadow(r_newrefdef.entities[i]);
	}

	// Restore render state and reset uniforms to defaults.
	glDisable(GL_STENCIL_TEST);
	glStencilMask(0xFF);
	glDisable(GL_POLYGON_OFFSET_FILL);
	glPolygonOffset(0.0f, 0.0f);
	glDepthMask(GL_TRUE);
	glDisable(GL_BLEND);
	glEnable(GL_CULL_FACE);

	GL3_UseShader(gl3state.shader3D);
	glUniform4f(gl3state.uni3D_color, 1.0f, 1.0f, 1.0f, 1.0f);
	GL3_UpdateModelview3D(r_world_matrix);
}
