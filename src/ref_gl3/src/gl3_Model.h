//
// gl3_Model.h
//
// Copyright 1998 Raven Software
//

#pragma once

#include "qfiles.h"

#pragma region ========================== BRUSH MODELS ==========================

// Q2 counterpart
typedef struct
{
	vec3_t position;
} mvertex_t;

// Q2 counterpart
typedef struct
{
	vec3_t mins;
	vec3_t maxs;
	vec3_t origin;	// For sounds or lights.
	float radius;
	int headnode;
	int visleafs;	// Not including the solid leaf 0.
	int firstface;
	int numfaces;
} mmodel_t;

#define SIDE_FRONT			0
#define SIDE_BACK			1
#define SIDE_ON				2

#define SURF_PLANEBACK		2
#define SURF_DRAWSKY		4
#define SURF_SKIPDRAW		8
#define SURF_DRAWTURB		16

// Q2 counterpart
typedef struct
{
	ushort v[2];
	uint cachededgeoffset;
} medge_t;

// Q2 counterpart
typedef struct mtexinfo_s
{
	float vecs[2][4];
	int flags;
	int numframes;
	struct mtexinfo_s* next; // Animation chain.
	image_t* image;
} mtexinfo_t;

#define	VERTEXSIZE	7

// Q2 counterpart
typedef struct glpoly_s
{
	struct glpoly_s* next;
	struct glpoly_s* chain;
	int numverts;
	int flags;
	float verts[4][VERTEXSIZE]; // Variable sized (xyz s1t1 s2t2).
} glpoly_t;

// Q2 counterpart
typedef struct msurface_s
{
	int visframe;

	cplane_t* plane;
	int flags;

	int firstedge;
	int numedges;

	short texturemins[2];
	short extents[2];

	int light_s;
	int light_t;

	int dlight_s;
	int dlight_t;

	glpoly_t* polys;
	struct msurface_s* texturechain;
	struct msurface_s* lightmapchain;

	mtexinfo_t* texinfo;

	int dlightframe;
	int dlightbits;

	int lightmaptexturenum;
	byte styles[MAXLIGHTMAPS];
	float cached_light[MAXLIGHTMAPS];
	byte* samples;
} msurface_t;

// Q2 counterpart
typedef struct mnode_s
{
	int contents;
	int visframe;

	float minmaxs[6];

	struct mnode_s* parent;

	cplane_t* plane;
	struct mnode_s* children[2];

	ushort firstsurface;
	ushort numsurfaces;
} mnode_t;

// Q2 counterpart
typedef struct mleaf_s
{
	int contents;
	int visframe;

	float minmaxs[6];

	struct mnode_s* parent;

	int cluster;
	int area;

	msurface_t** firstmarksurface;
	int nummarksurfaces;
} mleaf_t;

#pragma endregion

#pragma region ========================== WHOLE MODEL ==========================

typedef enum
{
	mod_bad,
	mod_brush,
	mod_sprite,
	mod_alias,
	mod_unknown,
	mod_fmdl,
	mod_book
} modtype_t;

typedef struct model_s
{
	char name[MAX_QPATH];
	int registration_sequence;
	modtype_t type;

	vec3_t mins;
	vec3_t maxs;
	float radius;

	int firstmodelsurface;
	int nummodelsurfaces;

	int numsubmodels;
	mmodel_t* submodels;

	int numplanes;
	cplane_t* planes;

	int numleafs;
	mleaf_t* leafs;

	int numvertexes;
	mvertex_t* vertexes;

	int numedges;
	medge_t* edges;

	int numnodes;
	int firstnode;
	mnode_t* nodes;

	int numtexinfo;
	mtexinfo_t* texinfo;

	int numsurfaces;
	msurface_t* surfaces;

	int numsurfedges;
	int* surfedges;

	int nummarksurfaces;
	msurface_t** marksurfaces;

	dvis_t* vis;
	byte* lightdata;

	image_t* skins[MAX_FRAMES];

	int extradatasize;
	void* extradata;
} model_t;

#pragma endregion

extern int registration_sequence;

extern void Mod_Init(void);
extern void Mod_FreeAll(void);

extern mleaf_t* Mod_PointInLeaf(vec3_t p, const model_t* model);
extern byte* Mod_ClusterPVS(int cluster, const model_t* model);
extern void Mod_Modellist_f(void);

extern void RI_BeginRegistration(const char* model);
extern struct model_s* RI_RegisterModel(const char* name);
extern void RI_EndRegistration(void);
