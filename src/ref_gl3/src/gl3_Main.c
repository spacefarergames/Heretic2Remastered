//
// gl3_Main.c
//
// OpenGL 3.3 Core Profile renderer - main entry point.
//

#include "gl3_Draw.h"
#include "gl3_FlexModel.h"
#include "gl3_Image.h"
#include "gl3_Jobs.h"
#include "gl3_Light.h"
#include "gl3_Misc.h"
#include "gl3_SDL.h"
#include "gl3_Shadow.h"
#include "gl3_Shaders.h"
#include "gl3_Sky.h"
#include "gl3_Sprite.h"
#include "gl3_Surface.h"
#include "gl3_Local.h"
#include "ParticleFlags.h"
#include "Reference.h"
#include "turbsin.h"
#include "Vector.h"
#include "vid.h"
#include <math.h>
#include <stdlib.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#define REF_DECLSPEC	__declspec(dllexport)

viddef_t viddef;
refimport_t ri;

model_t* r_worldmodel;

float gldepthmin;
float gldepthmax;

glconfig_t gl_config;
glstate_t gl_state;
gl3state_t gl3state;

// View origin.
vec3_t vup;
vec3_t vpn;
vec3_t vright;
vec3_t r_origin;

float r_world_matrix[16];
float r_projection_matrix[16];
cplane_t frustum[4];

refdef_t r_newrefdef;

int r_framecount;

int r_viewcluster;
int r_viewcluster2;
int r_oldviewcluster;
int r_oldviewcluster2;

int c_brush_polys;
int c_alias_polys;

static float v_blend[4];

#pragma region ========================== CVARS ==========================

cvar_t* r_norefresh;
cvar_t* r_fullbright;
cvar_t* r_drawentities;
cvar_t* r_drawworld;
cvar_t* r_novis;
cvar_t* r_nocull;
cvar_t* r_lerpmodels;
static cvar_t* r_speeds;
cvar_t* r_vsync;

cvar_t* r_lightlevel;

cvar_t* r_farclipdist;
cvar_t* r_fog;
cvar_t* r_fog_mode;
cvar_t* r_fog_density;
cvar_t* r_fog_startdist;
static cvar_t* r_fog_color_r;
static cvar_t* r_fog_color_g;
static cvar_t* r_fog_color_b;
static cvar_t* r_fog_color_a;
cvar_t* r_fog_lightmap_adjust;
cvar_t* r_fog_underwater;
static cvar_t* r_fog_underwater_mode;
static cvar_t* r_fog_underwater_density;
static cvar_t* r_fog_underwater_startdist;
static cvar_t* r_fog_underwater_color_r;
static cvar_t* r_fog_underwater_color_g;
static cvar_t* r_fog_underwater_color_b;
static cvar_t* r_fog_underwater_color_a;
static cvar_t* r_underwater_color;
cvar_t* r_frameswap;
cvar_t* r_references;
static cvar_t* r_hdr_exposure;
static cvar_t* r_bloom;
static cvar_t* r_bloom_threshold;
static cvar_t* r_bloom_strength;
static cvar_t* r_ssao;
static cvar_t* r_ssao_radius;
static cvar_t* r_ssao_bias;
static cvar_t* r_ssao_strength;
static cvar_t* r_shadows;
cvar_t* r_reflections;
cvar_t* r_hd_textures;
cvar_t* r_antialiasing;

cvar_t* gl_noartifacts;

cvar_t* gl_modulate;
cvar_t* gl_lightmap;
cvar_t* gl_dynamic;
cvar_t* gl_nobind;
cvar_t* gl_showtris;
static cvar_t* gl_clear;
static cvar_t* gl_cull;
cvar_t* gl_flashblend;
cvar_t* gl_texturemode;
cvar_t* gl_lockpvs;
cvar_t* gl_minlight;

cvar_t* gl_drawflat;
cvar_t* gl_trans33;
cvar_t* gl_trans66;
cvar_t* gl_bookalpha;

cvar_t* gl_drawbuffer;
cvar_t* gl_saturatelighting;

cvar_t* vid_gamma;
cvar_t* vid_brightness;
cvar_t* vid_contrast;
static cvar_t* vid_textures_refresh_required;

cvar_t* vid_ref;

cvar_t* vid_mode;
cvar_t* menus_active;
cvar_t* cl_camera_under_surface;
cvar_t* quake_amount;

#pragma endregion

// ============================================================
// GL3 2D/3D Setup.
// ============================================================

static void R_SetupGL2D(void)
{
	glViewport(0, 0, viddef.width, viddef.height);
	glDisable(GL_DEPTH_TEST);
	glDisable(GL_CULL_FACE);
	glDisable(GL_BLEND);

	R_SelectTexture(GL_TEXTURE0);
	GL3_UpdateProjection2D((float)viddef.width, (float)viddef.height);
}

static void R_Fog(void)
{
	int mode = (int)r_fog_mode->value;
	if (mode < 0) mode = 0;
	if (mode > 2) mode = 2;

	const float fog_r = r_fog_color_r->value;
	const float fog_g = r_fog_color_g->value;
	const float fog_b = r_fog_color_b->value;

	GL3_SetFog(1, mode, fog_r, fog_g, fog_b, r_fog_density->value, r_fog_startdist->value, r_farclipdist->value);

	glClearColor(fog_r, fog_g, fog_b, r_fog_color_a->value);
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
}

static void R_WaterFog(void)
{
	int mode = (int)r_fog_underwater_mode->value;
	if (mode < 0) mode = 0;
	if (mode > 2) mode = 2;

	const float fog_r = r_fog_underwater_color_r->value;
	const float fog_g = r_fog_underwater_color_g->value;
	const float fog_b = r_fog_underwater_color_b->value;

	GL3_SetFog(1, mode, fog_r, fog_g, fog_b, r_fog_underwater_density->value, r_fog_underwater_startdist->value, r_farclipdist->value);

	glClearColor(fog_r, fog_g, fog_b, r_fog_underwater_color_a->value);
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
}

static void R_Clear(void)
{
	gldepthmin = 0.0f;
	gldepthmax = 1.0f;
	glDepthFunc(GL_LEQUAL);
	glDepthRange((double)gldepthmin, (double)gldepthmax);

	if ((int)cl_camera_under_surface->value)
	{
		R_WaterFog();
	}
	else if ((int)r_fog->value)
	{
		R_Fog();
	}
	else
	{
		GL3_DisableFog();

		if (gl_clear != NULL && (int)gl_clear->value)
			glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
		else
			glClear(GL_DEPTH_BUFFER_BIT);
	}
}

static void R_ScreenFlash(const paletteRGBA_t color)
{
	if (color.a == 0)
		return;

	glDepthMask(GL_FALSE);

	// Draw a fullscreen color overlay using the 2D shader.
	Draw_FadeScreen(color);

	glDepthMask(GL_TRUE);

	ri.Deactivate_Screen_Flash();
}

// ============================================================
// Registration (model/texture loading).
// ============================================================

static void R_Register(void)
{
	r_norefresh = ri.Cvar_Get("r_norefresh", "0", 0);
	r_fullbright = ri.Cvar_Get("r_fullbright", "0", 0);
	r_drawentities = ri.Cvar_Get("r_drawentities", "1", 0);
	r_drawworld = ri.Cvar_Get("r_drawworld", "1", 0);
	r_novis = ri.Cvar_Get("r_novis", "0", 0);
	r_nocull = ri.Cvar_Get("r_nocull", "0", 0);
	r_lerpmodels = ri.Cvar_Get("r_lerpmodels", "1", 0);
	r_speeds = ri.Cvar_Get("r_speeds", "0", 0);
	r_vsync = ri.Cvar_Get("r_vsync", "1", CVAR_ARCHIVE);

	r_lightlevel = ri.Cvar_Get("r_lightlevel", "0", 0);

	r_farclipdist = ri.Cvar_Get("r_farclipdist", "4096.0", 0);
	r_fog = ri.Cvar_Get("r_fog", "0", 0);
	r_fog_mode = ri.Cvar_Get("r_fog_mode", "1", 0);
	r_fog_density = ri.Cvar_Get("r_fog_density", "0.004", 0);
	r_fog_startdist = ri.Cvar_Get("r_fog_startdist", "50.0", 0);
	r_fog_color_r = ri.Cvar_Get("r_fog_color_r", "1.0", 0);
	r_fog_color_g = ri.Cvar_Get("r_fog_color_g", "1.0", 0);
	r_fog_color_b = ri.Cvar_Get("r_fog_color_b", "1.0", 0);
	r_fog_color_a = ri.Cvar_Get("r_fog_color_a", "0.0", 0);
	r_fog_lightmap_adjust = ri.Cvar_Get("r_fog_lightmap_adjust", "5.0", 0);
	r_fog_underwater_mode = ri.Cvar_Get("r_fog_underwater_mode", "1", 0);
	r_fog_underwater_density = ri.Cvar_Get("r_fog_underwater_density", "0.0015", 0);
	r_fog_underwater_startdist = ri.Cvar_Get("r_fog_underwater_startdist", "100.0", 0);
	r_fog_underwater_color_r = ri.Cvar_Get("r_fog_underwater_color_r", "1.0", 0);
	r_fog_underwater_color_g = ri.Cvar_Get("r_fog_underwater_color_g", "1.0", 0);
	r_fog_underwater_color_b = ri.Cvar_Get("r_fog_underwater_color_b", "1.0", 0);
	r_fog_underwater_color_a = ri.Cvar_Get("r_fog_underwater_color_a", "0.0", 0);
	r_underwater_color = ri.Cvar_Get("r_underwater_color", "0x70c06000", 0);
	r_frameswap = ri.Cvar_Get("r_frameswap", "1.0", 0);
	r_references = ri.Cvar_Get("r_references", "1.0", 0);
	r_hdr_exposure    = ri.Cvar_Get("r_hdr_exposure",    "1.0", CVAR_ARCHIVE);
	r_bloom           = ri.Cvar_Get("r_bloom",           "1",   CVAR_ARCHIVE);
	r_bloom_threshold = ri.Cvar_Get("r_bloom_threshold", "0.8", CVAR_ARCHIVE);
	r_bloom_strength  = ri.Cvar_Get("r_bloom_strength",  "0.5", CVAR_ARCHIVE);
	r_ssao            = ri.Cvar_Get("r_ssao",            "1",   CVAR_ARCHIVE);
	r_ssao_radius     = ri.Cvar_Get("r_ssao_radius",     "16.0",CVAR_ARCHIVE);
	r_ssao_bias       = ri.Cvar_Get("r_ssao_bias",       "0.5", CVAR_ARCHIVE);
	r_ssao_strength   = ri.Cvar_Get("r_ssao_strength",   "1.0", CVAR_ARCHIVE);
	r_shadows         = ri.Cvar_Get("r_shadows",         "1",   CVAR_ARCHIVE);
	r_reflections     = ri.Cvar_Get("r_reflections",     "1",   CVAR_ARCHIVE);
	r_hd_textures     = ri.Cvar_Get("r_hd_textures",     "1",   CVAR_ARCHIVE);
	r_antialiasing    = ri.Cvar_Get("r_antialiasing",    "0",   CVAR_ARCHIVE);

	gl_noartifacts = ri.Cvar_Get("gl_noartifacts", "0", 0);

	gl_modulate = ri.Cvar_Get("gl_modulate", "1", CVAR_ARCHIVE);
	gl_lightmap = ri.Cvar_Get("gl_lightmap", "0", 0);
	gl_dynamic = ri.Cvar_Get("gl_dynamic", "1", 0);
	gl_nobind = ri.Cvar_Get("gl_nobind", "0", 0);
	gl_showtris = ri.Cvar_Get("gl_showtris", "0", 0);
	gl_clear = ri.Cvar_Get("gl_clear", "0", 0);
	gl_cull = ri.Cvar_Get("gl_cull", "1", 0);
	gl_flashblend = ri.Cvar_Get("gl_flashblend", "0", 0);
	gl_texturemode = ri.Cvar_Get("gl_texturemode", "GL_LINEAR_MIPMAP_NEAREST", CVAR_ARCHIVE);
	gl_lockpvs = ri.Cvar_Get("gl_lockpvs", "0", 0);
	gl_minlight = ri.Cvar_Get("gl_minlight", "0", CVAR_ARCHIVE);

	gl_drawflat = ri.Cvar_Get("gl_drawflat", "0", 0);
	gl_trans33 = ri.Cvar_Get("gl_trans33", "0.33", 0);
	gl_trans66 = ri.Cvar_Get("gl_trans66", "0.66", 0);
	gl_bookalpha = ri.Cvar_Get("gl_bookalpha", "1.0", 0);

	gl_drawbuffer = ri.Cvar_Get("gl_drawbuffer", "GL_BACK", 0);
	gl_saturatelighting = ri.Cvar_Get("gl_saturatelighting", "0", 0);

	vid_gamma = ri.Cvar_Get("vid_gamma", "0.5", CVAR_ARCHIVE);
	vid_brightness = ri.Cvar_Get("vid_brightness", "0.5", CVAR_ARCHIVE);
	vid_contrast = ri.Cvar_Get("vid_contrast", "0.5", CVAR_ARCHIVE);
	vid_textures_refresh_required = ri.Cvar_Get("vid_textures_refresh_required", "0", 0);

	vid_ref = ri.Cvar_Get("vid_ref", "gl3", CVAR_ARCHIVE);

	vid_mode = ri.Cvar_Get("vid_mode", "0", CVAR_ARCHIVE); // 0 = Desktop resolution.
	menus_active = ri.Cvar_Get("menus_active", "0", 0);
	cl_camera_under_surface = ri.Cvar_Get("cl_camera_under_surface", "0", 0);
	quake_amount = ri.Cvar_Get("quake_amount", "0", 0);

	ri.Cmd_AddCommand("imagelist", R_ImageList_f);
	ri.Cmd_AddCommand("screenshot", R_ScreenShot_f);
	ri.Cmd_AddCommand("modellist", Mod_Modellist_f);
	ri.Cmd_AddCommand("gl_strings", R_Strings_f);

	R_InitGammaTable();
}

// Changes the video mode.
static rserr_t SetMode_impl(int* pwidth, int* pheight, const int mode)
{
	ri.Con_Printf(PRINT_ALL, "Setting mode %d:", mode);

	if (!ri.Vid_GetModeInfo(pwidth, pheight, mode))
	{
		ri.Con_Printf(PRINT_ALL, " invalid mode\n");
		return RSERR_INVALID_MODE;
	}

	ri.Con_Printf(PRINT_ALL, " %dx%d\n", *pwidth, *pheight);

	return (ri.GLimp_InitGraphics(*pwidth, *pheight) ? RSERR_OK : RSERR_INVALID_MODE);
}

static qboolean R_SetMode(void)
{
	rserr_t err = SetMode_impl(&viddef.width, &viddef.height, (int)vid_mode->value);

	if (err == RSERR_OK)
	{
		gl_state.prev_mode = (int)vid_mode->value;
		return true;
	}

	if (err == RSERR_INVALID_MODE)
	{
		ri.Con_Printf(PRINT_ALL, "ref_gl3::R_SetMode() - invalid mode\n");

		if ((int)vid_mode->value == gl_state.prev_mode)
			return false;

		ri.Cvar_SetValue("vid_mode", (float)gl_state.prev_mode);
		vid_mode->modified = false;
	}
	else
	{
		ri.Con_Printf(PRINT_ALL, "ref_gl3::R_SetMode() - unknown error %i!\n", err);
		return false;
	}

	err = SetMode_impl(&viddef.width, &viddef.height, (int)vid_mode->value);

	if (err != RSERR_OK)
	{
		ri.Con_Printf(PRINT_ALL, "ref_gl3::R_SetMode() - could not revert to safe mode\n");
		return false;
	}

	return true;
}

// ============================================================
// Init / Shutdown.
// ============================================================

static qboolean RI_Init(void)
{
	for (int j = 0; j < 256; j++)
		turbsin[j] *= 0.5f;

	ri.Con_Printf(PRINT_ALL, "Refresh: "REF_TITLE"\n");
	R_Register();

	// Set our "safe" mode.
	gl_state.prev_mode = 1;

	// Create the window and set up the context.
	if (!R_SetMode())
	{
		ri.Con_Printf(PRINT_ALL, "ref_gl3::RI_Init() - could not R_SetMode()\n");
		return false;
	}

	// Get our various GL strings.
	gl_config.vendor_string = (const char*)glGetString(GL_VENDOR);
	ri.Con_Printf(PRINT_ALL, "GL_VENDOR: %s\n", gl_config.vendor_string);

	gl_config.renderer_string = (const char*)glGetString(GL_RENDERER);
	ri.Con_Printf(PRINT_ALL, "GL_RENDERER: %s\n", gl_config.renderer_string);

	gl_config.version_string = (const char*)glGetString(GL_VERSION);
	ri.Con_Printf(PRINT_ALL, "GL_VERSION: %s\n", gl_config.version_string);

	// GL 3.3 core uses glGetStringi for extensions.
	gl_config.extensions_string = "";

	// Anisotropic texture filtering.
	if (GLAD_GL_EXT_texture_filter_anisotropic)
	{
		glGetFloatv(GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT, &gl_config.max_anisotropy);
		ri.Con_Printf(PRINT_ALL, "Max. anisotropy: %i\n", (int)gl_config.max_anisotropy);
	}
	else
	{
		gl_config.max_anisotropy = 0.0f;
		ri.Con_Printf(PRINT_ALL, "Anisotropic filtering not supported.\n");
	}

	// Check max texture size.
	int max_texture_size;
	glGetIntegerv(GL_MAX_TEXTURE_SIZE, &max_texture_size);
	if (max_texture_size < 512)
	{
		ri.Con_Printf(PRINT_ALL, "ref_gl3::RI_Init() - maximum supported texture size too low! Expected at least 512, got %i\n", max_texture_size);
		return false;
	}

	// Initialize GL3 shaders.
	memset(&gl3state, 0, sizeof(gl3state));
	if (!GL3_InitShaders())
	{
		ri.Con_Printf(PRINT_ALL, "ref_gl3::RI_Init() - could not initialize shaders\n");
		return false;
	}

	GL3_InitFBO(viddef.width, viddef.height);
	GL3_InitBloom(viddef.width, viddef.height);
	GL3_InitSSAO(viddef.width, viddef.height);
	GL3_InitReflect(viddef.width, viddef.height);

	// Initialize job system for multithreading.
	GL3_InitJobs(0);

	R_SetDefaultState();
	R_InitImages();
	Mod_Init();
	Draw_InitLocal();
	R_InitShadows();

	const GLenum err = glGetError();
	if (err != GL_NO_ERROR)
	{
		ri.Con_Printf(PRINT_ALL, "glGetError() = 0x%x\n", err);
		return false;
	}

	return true;
}

static void RI_Shutdown(void)
{
	ShutdownFonts();

	ri.Cmd_RemoveCommand("modellist");
	ri.Cmd_RemoveCommand("screenshot");
	ri.Cmd_RemoveCommand("imagelist");
	ri.Cmd_RemoveCommand("gl_strings");

	Mod_FreeAll();
	R_ShutdownImages();
	R_ShutdownShadows();
	GL3_ShutdownReflect();
	GL3_ShutdownSSAO();
	GL3_ShutdownBloom();
	GL3_ShutdownFBO();
	GL3_ShutdownShaders();
	GL3_ShutdownJobs();

	// Shutdown OS-specific OpenGL stuff.
	RI_ShutdownContext();
}

static void RI_BeginFrame(const float camera_separation)
{
	// Toggle HD textures on the fly (must run before gamma/texture refresh so is_hd flags are up-to-date).
	if (r_hd_textures->modified)
	{
		R_HDTextureToggle();
		r_hd_textures->modified = false;
	}

	// Changed.
	if (vid_gamma->modified || vid_brightness->modified || vid_contrast->modified)
	{
		R_InitGammaTable();
		R_GammaAffect(false);

		vid_gamma->modified = false;
		vid_brightness->modified = false;
		vid_contrast->modified = false;
	}
	else if (vid_textures_refresh_required->value == 1.0f)
	{
		R_GammaAffect(true);
		ri.Cvar_SetValue("vid_textures_refresh_required", 0.0f);
	}

	// Go into 2D mode.
	R_SetupGL2D();

	// Texturemode stuff.
	if (gl_texturemode->modified)
	{
		R_TextureMode(gl_texturemode->string);
		gl_texturemode->modified = false;
	}

	// Swapinterval stuff.
	if (r_vsync->modified)
	{
		R_SetVsync();
		r_vsync->modified = false;
	}

	// Clear screen if desired.
	R_Clear();
}

// ============================================================
// 3D rendering.
// ============================================================

static byte R_SignbitsForPlane(const cplane_t* plane)
{
	byte bits = 0;
	for (int i = 0; i < 3; i++)
		if (plane->normal[i] < 0.0f)
			bits |= 1 << i;
	return bits;
}

static void R_SetFrustum(void)
{
	RotatePointAroundVector(frustum[0].normal, vup,    vpn, -(90.0f - r_newrefdef.fov_x * 0.5f));
	RotatePointAroundVector(frustum[1].normal, vup,    vpn,   90.0f - r_newrefdef.fov_x * 0.5f);
	RotatePointAroundVector(frustum[2].normal, vright, vpn,   90.0f - r_newrefdef.fov_y * 0.5f);
	RotatePointAroundVector(frustum[3].normal, vright, vpn, -(90.0f - r_newrefdef.fov_y * 0.5f));

	for (int i = 0; i < 4; i++)
	{
		const float frustum_dist = VectorLength(frustum[i].normal);
		if (frustum_dist <= 0.999999f)
			ri.Con_Printf(PRINT_ALL, "Frustum normal dist %f < 1.0\n", (double)frustum_dist);

		frustum[i].type = PLANE_ANYZ;
		frustum[i].dist = DotProduct(r_origin, frustum[i].normal);
		frustum[i].signbits = R_SignbitsForPlane(&frustum[i]);
	}
}

static void R_SetupFrame(void)
{
	r_framecount++;

	VectorCopy(r_newrefdef.vieworg, r_origin);
	AngleVectors(r_newrefdef.viewangles, vpn, vright, vup);

	if (!(r_newrefdef.rdflags & RDF_NOWORLDMODEL))
	{
		r_oldviewcluster  = r_viewcluster;
		r_oldviewcluster2 = r_viewcluster2;

		const mleaf_t* leaf = Mod_PointInLeaf(r_origin, r_worldmodel);
		r_viewcluster  = leaf->cluster;
		r_viewcluster2 = r_viewcluster;

		vec3_t temp = VEC3_INIT(r_origin);
		if (leaf->contents == 0)
			temp[2] -= 16.0f;
		else
			temp[2] += 16.0f;

		leaf = Mod_PointInLeaf(temp, r_worldmodel);
		if (!(leaf->contents & CONTENTS_SOLID))
			r_viewcluster2 = leaf->cluster;
	}

	for (int i = 0; i < 4; i++)
		v_blend[i] = r_newrefdef.blend[i];

	c_brush_polys = 0;
	c_alias_polys = 0;

	if (r_newrefdef.rdflags & RDF_NOWORLDMODEL)
	{
		glEnable(GL_SCISSOR_TEST);
		glClearColor(0.3f, 0.3f, 0.3f, 1.0f);
		glScissor(r_newrefdef.x, viddef.height - r_newrefdef.height - r_newrefdef.y, r_newrefdef.width, r_newrefdef.height);
		glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
		glClearColor(1.0f, 0.0f, 0.5f, 0.5f);
		glDisable(GL_SCISSOR_TEST);
	}
}

static void R_SetupGL3D(void)
{
	const int xl = r_newrefdef.x;
	const int xr = r_newrefdef.x + r_newrefdef.width;
	const int yt = viddef.height - r_newrefdef.y;
	const int yb = viddef.height - (r_newrefdef.y + r_newrefdef.height);

	glViewport(xl, yb, xr - xl, yt - yb);

	// Projection: call GL3_UpdateProjection3D which also stores into r_projection_matrix.
	static const float zNear = 1.0f;
	const float zFar = r_farclipdist->value;
	const float aspect = (float)r_newrefdef.width / (float)r_newrefdef.height;
	GL3_UpdateProjection3D(r_newrefdef.fov_y, aspect, zNear, zFar);

	// Modelview: replicate GL1's matrix sequence
	// glRotatef(-90,X) * glRotatef(90,Z) * glRotatef(-va[2],X) * glRotatef(-va[0],Y) * glRotatef(-va[1],Z) * T(-vieworg)
	R_Mat4x4_Identity(r_world_matrix);
	R_Mat4x4_Rotate(r_world_matrix, -90.0f, 1.0f, 0.0f, 0.0f);
	R_Mat4x4_Rotate(r_world_matrix,  90.0f, 0.0f, 0.0f, 1.0f);
	R_Mat4x4_Rotate(r_world_matrix, -r_newrefdef.viewangles[2], 1.0f, 0.0f, 0.0f);
	R_Mat4x4_Rotate(r_world_matrix, -r_newrefdef.viewangles[0], 0.0f, 1.0f, 0.0f);
	R_Mat4x4_Rotate(r_world_matrix, -r_newrefdef.viewangles[1], 0.0f, 0.0f, 1.0f);
	R_Mat4x4_Translate(r_world_matrix, -r_newrefdef.vieworg[0], -r_newrefdef.vieworg[1], -r_newrefdef.vieworg[2]);
	GL3_UpdateModelview3D(r_world_matrix);

	glCullFace(GL_FRONT);

	if ((int)gl_cull->value)
		glEnable(GL_CULL_FACE);
	else
		glDisable(GL_CULL_FACE);

	glDisable(GL_BLEND);
	glEnable(GL_DEPTH_TEST);
}

static void R_DrawEntitiesOnList(void)
{
	if (!(int)r_drawentities->value)
		return;

	for (int i = 0; i < r_newrefdef.num_entities; i++)
	{
		entity_t* ent = r_newrefdef.entities[i];

		if (ent->model == NULL)
		{
			ri.Con_Printf(PRINT_ALL, "Attempt to draw NULL model\n");
			R_DrawNullModel(ent);
			continue;
		}

		const model_t* mdl = *ent->model;

		if (mdl == NULL)
		{
			R_DrawNullModel(ent);
			continue;
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
}

static void R_RenderReflection(const float water_z)
{
	if (!(int)r_reflections->value || gl3state.fboReflect == 0)
		return;
	if (r_worldmodel == NULL || (r_newrefdef.rdflags & RDF_NOWORLDMODEL))
		return;

	// Save main-frame state.
	const refdef_t saved_refdef = r_newrefdef;
	vec3_t saved_vpn, saved_vright, saved_vup;
	VectorCopy(vpn, saved_vpn);
	VectorCopy(vright, saved_vright);
	VectorCopy(vup, saved_vup);
	cplane_t saved_frustum[4];
	memcpy(saved_frustum, frustum, sizeof(frustum));
	const int saved_viewcluster  = r_viewcluster;
	const int saved_viewcluster2 = r_viewcluster2;
	msurface_t* const saved_alpha = R_GetAlphaSurfaces();
	R_SetAlphaSurfaces(NULL);

	// Build reflected refdef: flip camera Z across water plane, negate pitch+roll.
	r_newrefdef.vieworg[2] = 2.0f * water_z - r_newrefdef.vieworg[2];
	r_newrefdef.viewangles[0] = -r_newrefdef.viewangles[0];
	r_newrefdef.viewangles[2] = -r_newrefdef.viewangles[2];
	VectorCopy(r_newrefdef.vieworg, r_origin);
	AngleVectors(r_newrefdef.viewangles, vpn, vright, vup);
	R_SetFrustum();

	// Render reflected scene into fboReflect.
	glBindFramebuffer(GL_FRAMEBUFFER, gl3state.fboReflect);
	glViewport(0, 0, gl3state.reflect_width, gl3state.reflect_height);
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
	glCullFace(GL_BACK);

	const float aspect = (float)r_newrefdef.width / (float)r_newrefdef.height;
	GL3_UpdateProjection3D(r_newrefdef.fov_y, aspect, 1.0f, r_farclipdist->value);
	R_Mat4x4_Identity(r_world_matrix);
	R_Mat4x4_Rotate(r_world_matrix, -90.0f, 1.0f, 0.0f, 0.0f);
	R_Mat4x4_Rotate(r_world_matrix,  90.0f, 0.0f, 0.0f, 1.0f);
	R_Mat4x4_Rotate(r_world_matrix, -r_newrefdef.viewangles[2], 1.0f, 0.0f, 0.0f);
	R_Mat4x4_Rotate(r_world_matrix, -r_newrefdef.viewangles[0], 0.0f, 1.0f, 0.0f);
	R_Mat4x4_Rotate(r_world_matrix, -r_newrefdef.viewangles[1], 0.0f, 0.0f, 1.0f);
	R_Mat4x4_Translate(r_world_matrix, -r_newrefdef.vieworg[0], -r_newrefdef.vieworg[1], -r_newrefdef.vieworg[2]);
	GL3_UpdateModelview3D(r_world_matrix);

	R_SetReflectionPass(true);
	R_MarkLeaves();
	R_ResetBmodelTransforms();
	R_DrawWorld();
	R_SetReflectionPass(false);

	// Discard alpha surfaces accumulated during reflection.
	R_SetAlphaSurfaces(NULL);

	// Restore main FBO and state.
	glBindFramebuffer(GL_FRAMEBUFFER, gl3state.fbo3D != 0 ? gl3state.fbo3D : 0);

	r_newrefdef = saved_refdef;
	VectorCopy(r_newrefdef.vieworg, r_origin);
	VectorCopy(saved_vpn, vpn);
	VectorCopy(saved_vright, vright);
	VectorCopy(saved_vup, vup);
	memcpy(frustum, saved_frustum, sizeof(frustum));
	r_viewcluster  = saved_viewcluster;
	r_viewcluster2 = saved_viewcluster2;
	r_oldviewcluster  = -1;
	r_oldviewcluster2 = -1;
	R_SetAlphaSurfaces(saved_alpha);

	// Re-mark leaves and restore 3D matrices/viewport for main camera.
	R_MarkLeaves();
	R_SetupGL3D();
}

static const GLfloat particle_st_coords[NUM_PARTICLE_TYPES][4] =
{
	{ 0.00390625f, 0.00390625f, 0.02734375f, 0.02734375f },
	{ 0.03515625f, 0.00390625f, 0.05859375f, 0.02734375f },
	{ 0.06640625f, 0.00390625f, 0.08984375f, 0.02734375f },
	{ 0.09765625f, 0.00390625f, 0.12109375f, 0.02734375f },
	{ 0.00390625f, 0.03515625f, 0.02734375f, 0.05859375f },
	{ 0.03515625f, 0.03515625f, 0.05859375f, 0.05859375f },
	{ 0.06640625f, 0.03515625f, 0.08984375f, 0.05859375f },
	{ 0.09765625f, 0.03515625f, 0.12109375f, 0.05859375f },
	{ 0.00390625f, 0.06640625f, 0.02734375f, 0.08984375f },
	{ 0.03515625f, 0.06640625f, 0.05859375f, 0.08984375f },
	{ 0.06640625f, 0.06640625f, 0.08984375f, 0.08984375f },
	{ 0.09765625f, 0.06640625f, 0.12109375f, 0.08984375f },
	{ 0.00390625f, 0.09765625f, 0.02734375f, 0.12109375f },
	{ 0.03515625f, 0.09765625f, 0.05859375f, 0.12109375f },
	{ 0.06640625f, 0.09765625f, 0.08984375f, 0.12109375f },
	{ 0.09765625f, 0.09765625f, 0.12109375f, 0.12109375f },
	{ 0.12890625f, 0.00390625f, 0.18359375f, 0.05859375f },
	{ 0.19140625f, 0.00390625f, 0.24609375f, 0.05859375f },
	{ 0.12890625f, 0.06640625f, 0.18359375f, 0.12109375f },
	{ 0.19140625f, 0.06640625f, 0.24609375f, 0.12109375f },
	{ 0.00390625f, 0.12890625f, 0.12109375f, 0.24609375f },
	{ 0.12890625f, 0.12890625f, 0.24609375f, 0.24609375f },
	{ 0.25390625f, 0.00390625f, 0.37109375f, 0.12109375f },
	{ 0.37890625f, 0.00390625f, 0.49609375f, 0.12109375f },
	{ 0.25390625f, 0.12890625f, 0.37109375f, 0.24609375f },
	{ 0.37890625f, 0.12890625f, 0.49609375f, 0.24609375f },
	{ 0.00390625f, 0.25390625f, 0.24609375f, 0.49609375f },
	{ 0.25390625f, 0.25390625f, 0.49609375f, 0.49609375f },
	{ 0.50390625f, 0.00390625f, 0.74609375f, 0.24609375f },
	{ 0.75390625f, 0.00390625f, 0.99609375f, 0.24609375f },
	{ 0.50390625f, 0.25390625f, 0.74609375f, 0.49609375f },
	{ 0.75390625f, 0.25390625f, 0.87109375f, 0.37109375f },
	{ 0.87890625f, 0.25390625f, 0.99609375f, 0.37109375f },
	{ 0.75390625f, 0.37890625f, 0.87109375f, 0.49609375f },
	{ 0.87890625f, 0.37890625f, 0.99609375f, 0.49609375f },
	{ 0.00390625f, 0.50390625f, 0.24609375f, 0.74609375f },
	{ 0.00390625f, 0.50390625f, 0.24609375f, 0.74609375f },
	{ 0.25390625f, 0.50390625f, 0.37109375f, 0.62109375f },
	{ 0.37890625f, 0.50390625f, 0.43359375f, 0.55859375f },
	{ 0.44140625f, 0.50390625f, 0.49609375f, 0.55859375f },
	{ 0.37890625f, 0.56640625f, 0.43359375f, 0.62109375f },
	{ 0.44140625f, 0.56640625f, 0.49609375f, 0.62109375f },
	{ 0.25390625f, 0.62890625f, 0.30859375f, 0.68359375f },
	{ 0.31640625f, 0.62890625f, 0.37109375f, 0.68359375f },
	{ 0.25390625f, 0.69140625f, 0.30859375f, 0.74609375f },
	{ 0.31640625f, 0.69140625f, 0.37109375f, 0.74609375f },
	{ 0.37890625f, 0.62890625f, 0.43359375f, 0.68359375f },
	{ 0.44140625f, 0.62890625f, 0.49609375f, 0.68359375f },
	{ 0.37890625f, 0.69140625f, 0.43359375f, 0.74609375f },
	{ 0.44140625f, 0.69140625f, 0.49609375f, 0.74609375f },
	{ 0.00390625f, 0.75390625f, 0.24609375f, 0.99609375f },
	{ 0.25390625f, 0.75390625f, 0.49609375f, 0.99609375f },
	{ 0.50390625f, 0.50390625f, 0.62109375f, 0.62109375f },
	{ 0.62890625f, 0.50390625f, 0.74609375f, 0.62109375f },
	{ 0.50390625f, 0.62890625f, 0.62109375f, 0.74609375f },
	{ 0.62890625f, 0.62890625f, 0.74609375f, 0.74609375f },
	{ 0.75390625f, 0.50390625f, 0.99609375f, 0.74609375f },
	{ 0.50390625f, 0.75390625f, 0.74609375f, 0.99609375f },
	{ 0.75390625f, 0.75390625f, 0.87109375f, 0.87109375f },
	{ 0.87890625f, 0.75390625f, 0.99609375f, 0.87109375f },
	{ 0.75390625f, 0.87890625f, 0.87109375f, 0.99609375f },
	{ 0.87890625f, 0.87890625f, 0.99609375f, 0.99609375f }
};

// Job data for parallel particle vertex generation.
typedef struct particle_job_data_s
{
	const particle_t* particles;
	float* output_verts;
	int start_idx;
	int end_idx;
	qboolean alpha_particle;
	vec3_t vup_local;
	vec3_t vright_local;
} particle_job_data_t;

// Worker function to generate particle vertices on a job thread.
static void R_GenerateParticleVerticesJob(void* data)
{
	const particle_job_data_t* job = (particle_job_data_t*)data;
	const particle_t* p = &job->particles[job->start_idx];
	float* v = job->output_verts;

	for (int i = job->start_idx; i < job->end_idx; i++, p++)
	{
		vec3_t p_up, p_right;
		VectorScale(job->vup_local, p->scale, p_up);
		VectorScale(job->vright_local, p->scale, p_right);

		paletteRGBA_t c;
		if (p->type & PFL_LM_COLOR)
			c = R_ModulateRGBA(p->color, R_GetSpriteShadelight(p->origin, p->color.a));
		else
			c = p->color;

		if (job->alpha_particle)
		{
			c.r = (byte)((int)c.r * c.a / 255);
			c.g = (byte)((int)c.g * c.a / 255);
			c.b = (byte)((int)c.b * c.a / 255);
		}

		const byte p_type = (p->type & PFL_FLAG_MASK);
		const float cr = (float)c.r / 255.0f;
		const float cg = (float)c.g / 255.0f;
		const float cb = (float)c.b / 255.0f;
		const float ca = (float)c.a / 255.0f;
		const float s0 = particle_st_coords[p_type][0];
		const float t0 = particle_st_coords[p_type][1];
		const float s1 = particle_st_coords[p_type][2];
		const float t1 = particle_st_coords[p_type][3];

		// Generate 2 triangles (6 vertices) for this particle.
		// Triangle 1: top-left, top-right, bottom-right
		v[0]=p->origin[0]+p_up[0]; v[1]=p->origin[1]+p_up[1]; v[2]=p->origin[2]+p_up[2];
		v[3]=s0; v[4]=t0; v[5]=cr; v[6]=cg; v[7]=cb; v[8]=ca;
		v += 9;

		v[0]=p->origin[0]+p_right[0]; v[1]=p->origin[1]+p_right[1]; v[2]=p->origin[2]+p_right[2];
		v[3]=s1; v[4]=t0; v[5]=cr; v[6]=cg; v[7]=cb; v[8]=ca;
		v += 9;

		v[0]=p->origin[0]-p_up[0]; v[1]=p->origin[1]-p_up[1]; v[2]=p->origin[2]-p_up[2];
		v[3]=s1; v[4]=t1; v[5]=cr; v[6]=cg; v[7]=cb; v[8]=ca;
		v += 9;

		// Triangle 2: top-left, bottom-right, bottom-left
		v[0]=p->origin[0]+p_up[0]; v[1]=p->origin[1]+p_up[1]; v[2]=p->origin[2]+p_up[2];
		v[3]=s0; v[4]=t0; v[5]=cr; v[6]=cg; v[7]=cb; v[8]=ca;
		v += 9;

		v[0]=p->origin[0]-p_up[0]; v[1]=p->origin[1]-p_up[1]; v[2]=p->origin[2]-p_up[2];
		v[3]=s1; v[4]=t1; v[5]=cr; v[6]=cg; v[7]=cb; v[8]=ca;
		v += 9;

		v[0]=p->origin[0]-p_right[0]; v[1]=p->origin[1]-p_right[1]; v[2]=p->origin[2]-p_right[2];
		v[3]=s0; v[4]=t1; v[5]=cr; v[6]=cg; v[7]=cb; v[8]=ca;
		v += 9;
	}
}

static void R_DrawParticles(const int num_particles, const particle_t* particles, const qboolean alpha_particle)
{
	if (num_particles < 1)
		return;

	if (alpha_particle)
	{
		R_BindImage(r_aparticletexture);
		glBlendFunc(GL_ONE, GL_ONE);
	}
	else
	{
		R_BindImage(r_particletexture);
		glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	}

	glEnable(GL_BLEND);

	// Allocate batch buffer: 6 vertices per particle (2 triangles), 9 floats per vertex.
	// Maximum safe stack allocation - use heap for large batches.
	#define MAX_STACK_PARTICLES 512
	#define PARTICLE_VERTEX_COUNT 6
	#define PARTICLE_FLOATS_PER_VERTEX 9

	float stack_buffer[MAX_STACK_PARTICLES * PARTICLE_VERTEX_COUNT * PARTICLE_FLOATS_PER_VERTEX];
	float* batch_verts = stack_buffer;
	float* heap_buffer = NULL;

	if (num_particles > MAX_STACK_PARTICLES)
	{
		heap_buffer = (float*)malloc(num_particles * PARTICLE_VERTEX_COUNT * PARTICLE_FLOATS_PER_VERTEX * sizeof(float));
		batch_verts = heap_buffer;
	}

	// Use multithreading for large particle batches.
	const int num_threads = GL3_GetNumWorkerThreads();
	const int threading_threshold = 128; // Only multithread if enough particles.

	if (num_particles >= threading_threshold && num_threads > 1)
	{
		// Distribute particles across worker threads.
		const int particles_per_job = (num_particles + num_threads - 1) / num_threads;
		particle_job_data_t* job_data = (particle_job_data_t*)malloc(num_threads * sizeof(particle_job_data_t));
		job_handle_t* job_handles = (job_handle_t*)malloc(num_threads * sizeof(job_handle_t));

		for (int t = 0; t < num_threads; t++)
		{
			const int start_idx = t * particles_per_job;
			const int end_idx = min(start_idx + particles_per_job, num_particles);

			if (start_idx >= num_particles)
				break;

			job_data[t].particles = particles;
			job_data[t].output_verts = batch_verts + (start_idx * PARTICLE_VERTEX_COUNT * PARTICLE_FLOATS_PER_VERTEX);
			job_data[t].start_idx = start_idx;
			job_data[t].end_idx = end_idx;
			job_data[t].alpha_particle = alpha_particle;
			VectorCopy(vup, job_data[t].vup_local);
			VectorCopy(vright, job_data[t].vright_local);

			job_handles[t] = GL3_ScheduleJob(R_GenerateParticleVerticesJob, &job_data[t]);
		}

		// Wait for all jobs to complete.
		for (int t = 0; t < num_threads; t++)
		{
			if (job_handles[t].job_id != -1)
				GL3_WaitForJob(job_handles[t]);
		}

		free(job_handles);
		free(job_data);
	}
	else
	{
		// Sequential fallback for small batches.
		const particle_t* p = &particles[0];
		float* v = batch_verts;

		for (int i = 0; i < num_particles; i++, p++)
		{
			vec3_t p_up, p_right;
			VectorScale(vup, p->scale, p_up);
			VectorScale(vright, p->scale, p_right);

			paletteRGBA_t c;
			if (p->type & PFL_LM_COLOR)
				c = R_ModulateRGBA(p->color, R_GetSpriteShadelight(p->origin, p->color.a));
			else
				c = p->color;

			if (alpha_particle)
			{
				c.r = (byte)((int)c.r * c.a / 255);
				c.g = (byte)((int)c.g * c.a / 255);
				c.b = (byte)((int)c.b * c.a / 255);
			}

			const byte p_type = (p->type & PFL_FLAG_MASK);
			const float cr = (float)c.r / 255.0f;
			const float cg = (float)c.g / 255.0f;
			const float cb = (float)c.b / 255.0f;
			const float ca = (float)c.a / 255.0f;
			const float s0 = particle_st_coords[p_type][0];
			const float t0 = particle_st_coords[p_type][1];
			const float s1 = particle_st_coords[p_type][2];
			const float t1 = particle_st_coords[p_type][3];

			// Generate 2 triangles (6 vertices) for this particle.
			// Triangle 1: top-left, top-right, bottom-right
			v[0]=p->origin[0]+p_up[0]; v[1]=p->origin[1]+p_up[1]; v[2]=p->origin[2]+p_up[2];
			v[3]=s0; v[4]=t0; v[5]=cr; v[6]=cg; v[7]=cb; v[8]=ca;
			v += 9;

			v[0]=p->origin[0]+p_right[0]; v[1]=p->origin[1]+p_right[1]; v[2]=p->origin[2]+p_right[2];
			v[3]=s1; v[4]=t0; v[5]=cr; v[6]=cg; v[7]=cb; v[8]=ca;
			v += 9;

			v[0]=p->origin[0]-p_up[0]; v[1]=p->origin[1]-p_up[1]; v[2]=p->origin[2]-p_up[2];
			v[3]=s1; v[4]=t1; v[5]=cr; v[6]=cg; v[7]=cb; v[8]=ca;
			v += 9;

			// Triangle 2: top-left, bottom-right, bottom-left
			v[0]=p->origin[0]+p_up[0]; v[1]=p->origin[1]+p_up[1]; v[2]=p->origin[2]+p_up[2];
			v[3]=s0; v[4]=t0; v[5]=cr; v[6]=cg; v[7]=cb; v[8]=ca;
			v += 9;

			v[0]=p->origin[0]-p_up[0]; v[1]=p->origin[1]-p_up[1]; v[2]=p->origin[2]-p_up[2];
			v[3]=s1; v[4]=t1; v[5]=cr; v[6]=cg; v[7]=cb; v[8]=ca;
			v += 9;

			v[0]=p->origin[0]-p_right[0]; v[1]=p->origin[1]-p_right[1]; v[2]=p->origin[2]-p_right[2];
			v[3]=s0; v[4]=t1; v[5]=cr; v[6]=cg; v[7]=cb; v[8]=ca;
			v += 9;
		}
	}

	// Draw all particles in a single batched call.
	GL3_UseShader(gl3state.shader3D);
	glBindVertexArray(gl3state.vao3D);
	glBindBuffer(GL_ARRAY_BUFFER, gl3state.vbo3D);
	glBufferData(GL_ARRAY_BUFFER, num_particles * PARTICLE_VERTEX_COUNT * PARTICLE_FLOATS_PER_VERTEX * sizeof(float), batch_verts, GL_STREAM_DRAW);
	glDrawArrays(GL_TRIANGLES, 0, num_particles * PARTICLE_VERTEX_COUNT);

	if (heap_buffer != NULL)
		free(heap_buffer);

	glDisable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
}

static void R_RenderView(const refdef_t* fd)
{
	if ((int)r_norefresh->value)
		return;

	r_newrefdef = *fd;

	if (r_worldmodel == NULL && !(r_newrefdef.rdflags & RDF_NOWORLDMODEL))
		ri.Sys_Error(ERR_DROP, "R_RenderView: NULL worldmodel");

	R_PushDlights();

	R_SetupFrame();
	R_SetFrustum();
	R_SetupGL3D();
	GL3_UpdateDlights();
	R_MarkLeaves();

	{
		float water_z;
		if (R_GetLastWaterPlaneZ(&water_z))
			R_RenderReflection(water_z);
	}

	R_ResetBmodelTransforms();
	R_DrawWorld();
	if ((int)r_shadows->value && (int)r_drawentities->value)
		R_DrawEntityShadows();
	R_DrawEntitiesOnList();
	R_RenderDlights();

	glDepthMask(GL_FALSE);
	R_SortAndDrawAlphaSurfaces();
	R_DrawParticles(r_newrefdef.num_particles, r_newrefdef.particles, false);
	R_DrawParticles(r_newrefdef.anum_particles, r_newrefdef.aparticles, true);
	glDepthMask(GL_TRUE);

	if ((int)r_speeds->value)
		ri.Con_Printf(PRINT_ALL, "%4i wpoly %4i epoly %i tex %i lmaps\n", c_brush_polys, c_alias_polys, c_visible_textures, c_visible_lightmaps);
}

static void R_SetLightLevel(void)
{
	if (!(r_newrefdef.rdflags & RDF_NOWORLDMODEL))
	{
		vec3_t shadelight;
		R_LightPoint(r_newrefdef.clientmodelorg, shadelight, true);
		r_lightlevel->value = max(shadelight[0], max(shadelight[1], shadelight[2])) * 150.0f / gl_modulate->value;
	}
}

static int RI_RenderFrame(const refdef_t* fd)
{
	paletteRGBA_t color;

	if ((int)cl_camera_under_surface->value)
		color.c = strtoul(r_underwater_color->string, NULL, 0);
	else
		color.c = ri.Is_Screen_Flashing();

	if (color.a != 255)
	{
		if (gl3state.fbo3D != 0)
		{
			glBindFramebuffer(GL_FRAMEBUFFER, gl3state.fbo3D);
			glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
			glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
		}

		R_RenderView(fd);
		R_SetLightLevel();

		if (gl3state.fbo3D != 0)
		{
			glBindFramebuffer(GL_FRAMEBUFFER, 0);
			const float bloom_strength = ((int)r_bloom->value != 0 && gl3state.fboBloomPingPong[1] != 0)
				? r_bloom_strength->value : 0.0f;
			const float ao_strength = ((int)r_ssao->value != 0 && gl3state.fboTexSSAOBlur != 0)
				? r_ssao_strength->value : 0.0f;
			if (ao_strength > 0.0f)
				GL3_RenderSSAO(r_ssao_radius->value, r_ssao_bias->value);
			GL3_RenderBloom(r_bloom_threshold->value, bloom_strength);
			GL3_CompositeHDR(viddef.width, viddef.height, r_hdr_exposure->value, bloom_strength, ao_strength);
		}

		R_SetupGL2D();

		if (color.a == 0)
			return 0;
	}

	R_ScreenFlash(color);

	return 0;
}

static int RI_GetReferencedID(const struct model_s* model)
{
	const fmdl_t* temp = model->extradata;

	if (model->type == mod_fmdl && temp->referenceType > REF_NULL && temp->referenceType < NUM_REFERENCED)
		return temp->referenceType;

	return REF_NULL;
}

static int RI_FindSurface(const vec3_t start, const vec3_t end, struct Surface_s* surface)
{
	// Stub - requires BSP traversal.
	return 0;
}

// ============================================================
// DLL Export.
// ============================================================

REF_DECLSPEC refexport_t GetRefAPI(const refimport_t rimp)
{
	refexport_t re;

	ri = rimp;

	re.api_version = REF_API_VERSION;
	re.title = REF_TITLE;

	re.BeginRegistration = RI_BeginRegistration;
	re.RegisterModel = RI_RegisterModel;
	re.RegisterSkin = RI_RegisterSkin;
	re.RegisterPic = Draw_FindPic;
	re.SetSky = RI_SetSky;
	re.EndRegistration = RI_EndRegistration;
	re.GetReferencedID = RI_GetReferencedID;

	re.RenderFrame = RI_RenderFrame;

	re.DrawGetPicSize = Draw_GetPicSize;
	re.DrawPic = Draw_Pic;
	re.DrawStretchPic = Draw_StretchPic;
	re.DrawChar = Draw_Char;
	re.DrawTileClear = Draw_TileClear;
	re.DrawFill = Draw_Fill;
	re.DrawFadeScreen = Draw_FadeScreen;

	re.DrawBigFont = Draw_BigFont;
	re.BF_Strlen = BF_Strlen;
	re.BookDrawPic = Draw_BookPic;
	re.DrawInitCinematic = Draw_InitCinematic;
	re.DrawCloseCinematic = Draw_CloseCinematic;
	re.DrawCinematic = Draw_Cinematic;
	re.Draw_Name = Draw_Name;

	re.Init = RI_Init;
	re.Shutdown = RI_Shutdown;

	re.BeginFrame = RI_BeginFrame;
	re.EndFrame = RI_EndFrame;
	re.FindSurface = RI_FindSurface;

	re.PrepareForWindow = RI_PrepareForWindow;
	re.InitContext = RI_InitContext;
	re.ShutdownContext = RI_ShutdownContext;

	return re;
}
