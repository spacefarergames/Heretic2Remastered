//
// gl3_Local.h
//
// OpenGL 4.6 Core Profile renderer for Heretic II Remaster.
//

#pragma once

#include <glad-GL4.6/glad.h> // Must be included before SDL.
#include "ref.h"

#define REF_TITLE			"OpenGL 4.6"

#define MAX_TEXTURE_UNITS	2

#define GL3_MAX_DLIGHTS		8		// Max per-pixel dynamic lights per draw call.

#define TEXNUM_LIGHTMAPS	1024
#define TEXNUM_IMAGES		1153

#define MAX_GLTEXTURES		2048

#define GL_TEX_SOLID_FORMAT GL_RGBA
#define GL_TEX_ALPHA_FORMAT GL_RGBA

#define LIGHTMAP_BYTES			4
#define LM_BLOCK_WIDTH			128
#define LM_BLOCK_HEIGHT			128
#define MAX_LIGHTMAPS			128
#define MAX_TALLWALL_LIGHTMAPS	512
#define GL_LIGHTMAP_FORMAT		GL_RGBA

#pragma region ========================== CVARS ==========================

extern cvar_t* r_norefresh;
extern cvar_t* r_fullbright;
extern cvar_t* r_drawentities;
extern cvar_t* r_drawworld;
extern cvar_t* r_novis;
extern cvar_t* r_nocull;
extern cvar_t* r_lerpmodels;
extern cvar_t* r_vsync;

extern cvar_t* r_lightlevel;

extern cvar_t* r_farclipdist;
extern cvar_t* r_fog;
extern cvar_t* r_fog_mode;
extern cvar_t* r_fog_density;
extern cvar_t* r_fog_startdist;
extern cvar_t* r_fog_lightmap_adjust;
extern cvar_t* r_fog_underwater;
extern cvar_t* r_fog_underwater_lightmap_adjust;
extern cvar_t* r_frameswap;
extern cvar_t* r_references;
extern cvar_t* r_reflections;
extern cvar_t* r_hd_textures;
extern cvar_t* r_antialiasing;

extern cvar_t* gl_noartifacts;

extern cvar_t* gl_modulate;
extern cvar_t* gl_lightmap;
extern cvar_t* gl_dynamic;
extern cvar_t* gl_nobind;
extern cvar_t* gl_showtris;
extern cvar_t* gl_flashblend;
extern cvar_t* gl_texturemode;
extern cvar_t* gl_lockpvs;
extern cvar_t* gl_minlight;

extern cvar_t* gl_drawflat;
extern cvar_t* gl_trans33;
extern cvar_t* gl_trans66;
extern cvar_t* gl_bookalpha;

extern cvar_t* gl_drawbuffer;
extern cvar_t* gl_saturatelighting;

extern cvar_t* vid_gamma;
extern cvar_t* vid_brightness;
extern cvar_t* vid_contrast;

extern cvar_t* vid_ref;

extern cvar_t* vid_mode;
extern cvar_t* menus_active;
extern cvar_t* cl_camera_under_surface;
extern cvar_t* quake_amount;

#pragma endregion

typedef enum
{
	it_skin = 1,
	it_sprite = 2,
	it_wall = 4,
	it_pic = 5,
	it_sky = 6
} imagetype_t;

typedef struct image_s
{
	struct image_s* next;
	char name[MAX_QPATH];
	imagetype_t type;
	int width;
	int height;
	int hd_width; // Actual HD texture pixel width (0 if not HD).
	int hd_height; // Actual HD texture pixel height (0 if not HD).
	int registration_sequence;
	struct msurface_s* texturechain;
	struct msurface_s* multitexturechain;
	int texnum;
	byte has_alpha;
	byte num_frames;
	byte is_hd; // True when loaded from HDTextures replacement PNG.
	char* hd_path; // Absolute path to HD replacement PNG (malloc'd, NULL if not HD).
	struct paletteRGB_s* palette;
} image_t;

typedef enum
{
	RSERR_OK,
	RSERR_INVALID_MODE
} rserr_t;

extern float gldepthmin;
extern float gldepthmax;

extern image_t gltextures[MAX_GLTEXTURES];
extern int numgltextures;

extern image_t* r_notexture;
extern image_t* r_particletexture;
extern image_t* r_aparticletexture;
extern image_t* r_reflecttexture;
extern image_t* r_font1;
extern image_t* r_font2;

#include "gl3_Model.h"

extern model_t* r_worldmodel;

#pragma region ========================== GL config stuff ==========================

typedef struct
{
	const char* renderer_string;
	const char* vendor_string;
	const char* version_string;
	const char* extensions_string;
	float max_anisotropy;
} glconfig_t;

typedef struct
{
	float inverse_intensity;
	qboolean fullscreen;
	qboolean minlight_set;

	int prev_mode;
	int lightmap_textures;
	int currenttextures[MAX_TEXTURE_UNITS];
	int currenttmu;
} glstate_t;

extern glconfig_t gl_config;
extern glstate_t gl_state;

extern vec3_t vup;
extern vec3_t vpn;
extern vec3_t vright;
extern vec3_t r_origin;

extern float r_world_matrix[16];
extern float r_projection_matrix[16];
extern cplane_t frustum[4];

extern refdef_t r_newrefdef;

extern int r_framecount;

extern int r_viewcluster;
extern int r_viewcluster2;
extern int r_oldviewcluster;
extern int r_oldviewcluster2;

extern int c_brush_polys;
extern int c_alias_polys;

extern int c_visible_lightmaps;
extern int c_visible_textures;

#pragma endregion

#pragma region ========================== GL3 SHADER STATE ==========================

// Shader program IDs.
typedef struct
{
	GLuint shader2D;			// 2D drawing (UI, HUD, console).
	GLuint shader3D;			// 3D generic (textured + per-vertex color, e.g. sprites, particles).
	GLuint shader3DColor;		// 3D colored, no texture.
	GLuint shader3DLightmap;	// 3D world surfaces: diffuse texture * lightmap texture.

	// Uniform locations for shader2D.
	GLint uni2D_projection;
	GLint uni2D_texture;
	GLint uni2D_color;

	// Uniform locations for shader3D.
	GLint uni3D_projection;
	GLint uni3D_modelview;
	GLint uni3D_texture;
	GLint uni3D_color;

	// Per-pixel dynamic light uniforms for shader3D.
	GLint uni3D_numDlights;
	GLint uni3D_dlightPosRad;	// vec4[GL3_MAX_DLIGHTS]: xyz=view-space pos, w=intensity.
	GLint uni3D_dlightColor;	// vec4[GL3_MAX_DLIGHTS]: xyz=rgb (0..1), w=unused.

	// Uniform locations for shader3DColor.
	GLint uni3DColor_projection;
	GLint uni3DColor_modelview;
	GLint uni3DColor_color;

	// Uniform locations for shader3DLightmap.
	GLint uni3DLM_projection;
	GLint uni3DLM_modelview;
	GLint uni3DLM_diffuse;
	GLint uni3DLM_lightmap;
	GLint uni3DLM_color;

	// VAO/VBO for 2D drawing
	GLuint vao2D;
	GLuint vbo2D;

	// VAO/VBO for lightmapped world surfaces (VERTEXSIZE=7 floats/vert: pos3 + tc2 + lmtc2).
	GLuint vao3DLM;
	GLuint vbo3DLM;

	// VAO/VBO for generic 3D drawing (9 floats/vert: pos3 + tc2 + col4).
	GLuint vao3D;
	GLuint vbo3D;

	// 1x1 white texture for color-only 2D drawing (Draw_Fill, Draw_FadeScreen).
	GLuint whiteTexture;

	// HDR FBO (full-screen): color = RGBA16F, depth = DEPTH24 texture (samplable for SSAO).
	GLuint fbo3D;
	GLuint fboTex3D;       // RGBA16F color texture.
	GLuint fboDepth3D;     // Depth texture (GL_DEPTH_COMPONENT24, sampled by SSAO shader).
	int    fbo_width;
	int    fbo_height;

	// Post-process pass-through shader + uniform locations.
	GLuint shaderPost;
	GLint  uniPost_hdrBuffer;
	GLint  uniPost_exposure;

	// Fog uniforms for shaderPost (depth-based post-process fog).
	GLint  uniPost_depthMap;
	GLint  uniPost_fogEnabled;
	GLint  uniPost_fogMode;
	GLint  uniPost_fogColor;
	GLint  uniPost_fogDensity;
	GLint  uniPost_fogStart;
	GLint  uniPost_fogEnd;
	GLint  uniPost_fogNearFar;

	// Full-screen quad VAO/VBO (4 verts: NDC pos2 + tc2).
	GLuint vaoFSQ;
	GLuint vboFSQ;

	// Bloom post-process FBOs (half-res: bloom_width x bloom_height, RGB16F, no depth).
	GLuint fboBloomExtract;
	GLuint fboTexBloomExtract;
	GLuint fboBloomPingPong[2];
	GLuint fboTexBloomPingPong[2];
	int    bloom_width;
	int    bloom_height;

	// Bloom shaders + uniform locations.
	GLuint shaderBloomExtract;
	GLint  uniBloomExtract_hdrBuffer;
	GLint  uniBloomExtract_threshold;

	GLuint shaderBloomBlur;
	GLint  uniBloomBlur_image;
	GLint  uniBloomBlur_horizontal;
	GLint  uniBloomBlur_texelSize;

	// Bloom contribution uniforms in shaderPost.
	GLint  uniPost_bloomBuffer;
	GLint  uniPost_bloomStrength;

	// AO contribution uniforms in shaderPost.
	GLint  uniPost_aoBuffer;
	GLint  uniPost_aoStrength;

	// SSAO post-process: full-res GL_R16F FBOs + shaders + noise texture.
	GLuint shaderSSAO;
	GLint  uniSSAO_depthMap;
	GLint  uniSSAO_noiseTex;
	GLint  uniSSAO_kernel;
	GLint  uniSSAO_projParams;
	GLint  uniSSAO_radius;
	GLint  uniSSAO_bias;
	GLint  uniSSAO_screenSize;

	GLuint shaderSSAOBlur;
	GLint  uniSSAOBlur_ssaoInput;
	GLint  uniSSAOBlur_texelSize;

	GLuint ssaoNoiseTex;
	GLuint fboSSAO;
	GLuint fboTexSSAO;
	GLuint fboSSAOBlur;
	GLuint fboTexSSAOBlur;

	// Projection params cached for SSAO reconstruction: {P[0], P[5], P[10], P[14]}.
	float projParams[4];

	// Planar water reflection FBO (half-res: reflect_width x reflect_height, RGBA16F, depth renderbuffer).
	GLuint fboReflect;
	GLuint fboTexReflect;     // RGBA16F color texture sampled by water shader.
	GLuint rboReflectDepth;   // Depth renderbuffer (not sampled).
	int    reflect_width;
	int    reflect_height;

	// Water surface shader (same 9-float vertex layout as shader3D, adds projective reflection).
	GLuint shaderWater;
	GLint  uniWater_projection;
	GLint  uniWater_modelview;
	GLint  uniWater_color;
	GLint  uniWater_reflectTex;   // unit 1: reflection FBO texture.
	GLint  uniWater_reflectAmt;   // blend factor.
	GLint  uniWater_time;

	// Lightmap GL texture IDs
	GLuint lightmap_textures[MAX_LIGHTMAPS + 1];
} gl3state_t;

extern gl3state_t gl3state;

#pragma endregion

#pragma region ========================== IMPORTED FUNCTIONS ==========================

extern refimport_t ri;

#pragma endregion
