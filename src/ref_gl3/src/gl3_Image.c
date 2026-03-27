//
// gl3_Image.c
//
// OpenGL 3.3 texture/image system.
//

#include "gl3_Image.h"
#include "gl3_Draw.h"
#include "gl3_Light.h"
#include <stb/stb_image.h>
#include <io.h> // _findfirst, _findnext, _findclose

image_t gltextures[MAX_GLTEXTURES];
int numgltextures;

#define NUM_HASHED_GLTEXTURES	256
static image_t* gltextures_hashed[NUM_HASHED_GLTEXTURES];

static byte gammatable[256];

int gl_filter_min = GL_NEAREST_MIPMAP_LINEAR;
int gl_filter_max = GL_LINEAR;

static paletteRGBA_t* upload_buffer = NULL;
static uint upload_buffer_size = 0;

typedef struct
{
	char* name;
	int	minimize;
	int maximize;
} glmode_t;

static glmode_t modes[] =
{
	{ "GL_NEAREST", GL_NEAREST, GL_NEAREST },
	{ "GL_LINEAR", GL_LINEAR, GL_LINEAR },
	{ "GL_NEAREST_MIPMAP_NEAREST", GL_NEAREST_MIPMAP_NEAREST, GL_NEAREST },
	{ "GL_LINEAR_MIPMAP_NEAREST", GL_LINEAR_MIPMAP_NEAREST, GL_LINEAR },
	{ "GL_NEAREST_MIPMAP_LINEAR", GL_NEAREST_MIPMAP_LINEAR, GL_NEAREST },
	{ "GL_LINEAR_MIPMAP_LINEAR", GL_LINEAR_MIPMAP_LINEAR, GL_LINEAR }
};

#define NUM_GL_MODES ((int)(sizeof(modes) / sizeof(glmode_t)))

void R_InitGammaTable(void)
{
	float contrast = 1.0f - vid_contrast->value;

	if (contrast > 0.5f)
		contrast = powf(contrast + 0.5f, 3.0f);
	else
		contrast = powf(contrast + 0.5f, 0.5f);

	gammatable[0] = 0;

	for (int i = 1; i < 256; i++)
	{
		float inf = 255.0f * powf(((float)i + 0.5f) / 255.5f, vid_gamma->value) + 0.5f;
		float sign;

		if (inf < 128.0f)
		{
			inf = 128.0f - inf;
			sign = -1.0f;
		}
		else
		{
			inf -= 128.0f;
			sign = 1.0f;
		}

		inf = (vid_brightness->value * 160.0f - 80.0f) + (powf(inf / 128.0f, contrast) * sign + 1.0f) * 128.0f;

		gammatable[i] = (byte)ClampI((int)inf, 0, 255);
	}
}

image_t* R_GetFreeImage(void)
{
	int index;
	image_t* image;

	for (index = 0, image = &gltextures[0]; index < numgltextures; index++, image++)
		if (image->registration_sequence == 0)
			break;

	if (index == numgltextures)
	{
		if (numgltextures == MAX_GLTEXTURES)
			ri.Sys_Error(ERR_DROP, "R_GetFreeImage: no free image_t slots!\n");

		numgltextures++;
	}

	memset(image, 0, sizeof(image_t));

	return image;
}

void R_SelectTexture(const GLenum texture)
{
	const int tmu = (texture == GL_TEXTURE1);
	if (tmu != gl_state.currenttmu)
	{
		gl_state.currenttmu = tmu;
		glActiveTexture(texture);
	}
}

void R_Bind(int texnum)
{
	if ((int)gl_nobind->value && draw_chars != NULL)
		texnum = draw_chars->texnum;

	if (gl_state.currenttextures[gl_state.currenttmu] != texnum)
	{
		gl_state.currenttextures[gl_state.currenttmu] = texnum;
		glBindTexture(GL_TEXTURE_2D, texnum);
	}
}

void R_BindImage(const image_t* image)
{
	int texnum;

	if ((int)gl_nobind->value && draw_chars != NULL)
		texnum = draw_chars->texnum;
	else
		texnum = image->texnum;

	if (gl_state.currenttextures[gl_state.currenttmu] != texnum)
	{
		gl_state.currenttextures[gl_state.currenttmu] = texnum;
		glBindTexture(GL_TEXTURE_2D, texnum);
	}
}

void R_MBind(const GLenum target, const int texnum)
{
	R_SelectTexture(target);
	if (gl_state.currenttextures[target == GL_TEXTURE1] != texnum)
		R_Bind(texnum);
}

void R_MBindImage(const GLenum target, const image_t* image)
{
	R_SelectTexture(target);
	if (gl_state.currenttextures[target == GL_TEXTURE1] != image->texnum)
		R_BindImage(image);
}

void R_TextureMode(const char* string)
{
	int cur_mode;

	for (cur_mode = 0; cur_mode < NUM_GL_MODES; cur_mode++)
		if (!Q_stricmp(modes[cur_mode].name, string))
			break;

	if (cur_mode == NUM_GL_MODES)
	{
		ri.Con_Printf(PRINT_ALL, "Bad texture filter name\n");
		return;
	}

	gl_filter_min = modes[cur_mode].minimize;
	gl_filter_max = modes[cur_mode].maximize;

	image_t* glt = &gltextures[0];
	for (int i = 0; i < numgltextures; i++, glt++)
	{
		if (glt->type != it_pic && glt->type != it_sky)
		{
			R_BindImage(glt);

			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, gl_filter_min);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, gl_filter_max);
		}
	}
}

void R_SetFilter(const image_t* image)
{
	switch (image->type)
	{
		case it_pic:
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
			break;

		case it_sky:
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, gl_filter_max);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, gl_filter_max);
			break;

		default:
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, gl_filter_min);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, gl_filter_max);
			break;
	}
}

void R_ImageList_f(void)
{
	int tex_count = 0;
	int tex_texels = 0;
	int sky_count = 0;
	int sky_texels = 0;
	int skin_count = 0;
	int skin_texels = 0;
	int sprite_count = 0;
	int sprite_texels = 0;
	int pic_count = 0;
	int pic_texels = 0;

	const char* palstrings[] = { "RGB", "PAL" };

	ri.Con_Printf(PRINT_ALL, "---------------------------\n");

	image_t* image = &gltextures[0];
	for (int i = 0; i < numgltextures; i++, image++)
	{
		switch (image->type)
		{
			case it_skin:
				ri.Con_Printf(PRINT_ALL, "M");
				skin_count++;
				skin_texels += image->width * image->height;
				break;

			case it_sprite:
				ri.Con_Printf(PRINT_ALL, "S");
				sprite_count++;
				sprite_texels += image->width * image->height;
				break;

			case it_wall:
				ri.Con_Printf(PRINT_ALL, "W");
				tex_count++;
				tex_texels += (image->width * image->height * 4) / 3;
				break;

			case it_pic:
				ri.Con_Printf(PRINT_ALL, "P");
				pic_count++;
				pic_texels += image->width * image->height;
				break;

			case it_sky:
				ri.Con_Printf(PRINT_ALL, "K");
				sky_count++;
				sky_texels += image->width * image->height;
				break;

			default:
				ri.Con_Printf(PRINT_ALL, "U%i", image->type, image->name);
				break;
		}

		ri.Con_Printf(PRINT_ALL, " %3i %3i %s %s\n", image->width, image->height, palstrings[image->palette != NULL], image->name);
	}

	ri.Con_Printf(PRINT_ALL, "-------------------------------\n");
	ri.Con_Printf(PRINT_ALL, "Total skin   : %i (%i texels)\n", skin_count, skin_texels);
	ri.Con_Printf(PRINT_ALL, "Total world  : %i (%i texels)\n", tex_count, tex_texels);
	ri.Con_Printf(PRINT_ALL, "Total sky    : %i (%i texels)\n", sky_count, sky_texels);
	ri.Con_Printf(PRINT_ALL, "Total sprite : %i (%i texels)\n", sprite_count, sprite_texels);
	ri.Con_Printf(PRINT_ALL, "Total pic    : %i (%i texels)\n", pic_count, pic_texels);
	ri.Con_Printf(PRINT_ALL, "-------------------------------\n");
}

#pragma region ========================== .M8 LOADING ==========================

void R_UploadPaletted(const int level, const byte* data, const paletteRGB_t* palette, const int width, const int height)
{
	const uint src_size = width * height;
	const uint dst_size = src_size * sizeof(paletteRGBA_t);

	if (dst_size > upload_buffer_size)
	{
		upload_buffer = realloc(upload_buffer, dst_size);

		if (upload_buffer == NULL)
			ri.Sys_Error(ERR_DROP, "R_UploadPaletted: failed to allocate upload buffer for %i x %i image!\n", width, height);

		upload_buffer_size = dst_size;
	}

	for (uint i = 0; i < src_size; i++)
	{
		const paletteRGB_t* src_p = &palette[data[i]];
		paletteRGBA_t* dst_p = &upload_buffer[i];

		dst_p->r = src_p->r;
		dst_p->g = src_p->g;
		dst_p->b = src_p->b;
		dst_p->a = 255;
	}

	glTexImage2D(GL_TEXTURE_2D, level, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, upload_buffer);
}

static void GrabPalette(paletteRGB_t* src, paletteRGB_t* dst)
{
	int i;
	paletteRGB_t* src_p;
	paletteRGB_t* dst_p;

	for (i = 0, src_p = src, dst_p = dst; i < PAL_SIZE; i++, src_p++, dst_p++)
	{
		dst_p->r = gammatable[src_p->r];
		dst_p->g = gammatable[src_p->g];
		dst_p->b = gammatable[src_p->b];
	}
}

static void FixPalette(const image_t* image)
{
	if (image->type != it_sprite || strstr(image->name, "Sprites/lens/flare") == NULL)
		return;

	if (strstr(image->name, "flare1_0.m8") != NULL)
	{
		memset(&image->palette[192], 0, sizeof(paletteRGB_t));
	}
	else if (strstr(image->name, "flare2_0.m8") != NULL)
	{
		memset(&image->palette[187], 0, sizeof(paletteRGB_t));
		memset(&image->palette[188], 0, sizeof(paletteRGB_t));
	}
	else if (strstr(image->name, "flare3_0.m8") != NULL)
	{
		memset(&image->palette[208], 0, sizeof(paletteRGB_t));
	}
	else if (strstr(image->name, "flare4_0.m8") != NULL)
	{
		memset(&image->palette[198], 0, sizeof(paletteRGB_t));
	}
	else if (strstr(image->name, "flare5_0.m8") != NULL)
	{
		memset(&image->palette[184], 0, sizeof(paletteRGB_t));
		memset(&image->palette[185], 0, sizeof(paletteRGB_t));
	}
}

static void R_UploadM8(miptex_t* mt, const image_t* image)
{
	int mip;
	for (mip = 0; mip < MIPLEVELS && mt->width[mip] > 0 && mt->height[mip] > 0; mip++)
		R_UploadPaletted(mip, (byte*)mt + mt->offsets[mip], image->palette, (int)mt->width[mip], (int)mt->height[mip]);

	// GL 3.3 Core: set max mip level to avoid incomplete texture.
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, max(mip - 1, 0));

	R_SetFilter(image);
}

static image_t* R_LoadM8(const char* name, const imagetype_t type)
{
	miptex_t* mt;
	ri.FS_LoadFile(name, (void**)&mt);

	if (mt == NULL)
	{
		ri.Con_Printf(PRINT_ALL, "R_LoadM8: can't load '%s'\n", name);
		return NULL;
	}

	if (mt->version != MIP_VERSION)
	{
		ri.Con_Printf(PRINT_ALL, "R_LoadM8: can't load '%s': invalid version (%i)\n", name, mt->version);
		ri.FS_FreeFile(mt);

		return NULL;
	}

	if (strlen(name) >= MAX_QPATH)
	{
		ri.Con_Printf(PRINT_ALL, "R_LoadM8: can't load '%s': filename too long\n", name);
		ri.FS_FreeFile(mt);

		return NULL;
	}

	paletteRGB_t* palette = malloc(sizeof(paletteRGB_t) * 256);
	GrabPalette(mt->palette, palette);

	image_t* image = R_GetFreeImage();
	strcpy_s(image->name, sizeof(image->name), name);
	image->registration_sequence = registration_sequence;
	image->width = (int)mt->width[0];
	image->height = (int)mt->height[0];
	image->type = type;
	image->palette = palette;
	image->has_alpha = false;
	image->num_frames = (byte)mt->value;

	glGenTextures(1, (GLuint*)&image->texnum);

	FixPalette(image);

	R_BindImage(image);
	R_UploadM8(mt, image);
	ri.FS_FreeFile(mt);

	return image;
}

#pragma endregion

#pragma region ========================== .M32 LOADING ==========================

static void R_ApplyGamma32(miptex32_t* mt)
{
	for (int mip = 0; mip < MIPLEVELS - 1; mip++)
	{
		const uint mip_size = mt->width[mip] * mt->height[mip];
		if (mip_size == 0)
			return;

		paletteRGBA_t* color = (paletteRGBA_t*)((byte*)mt + mt->offsets[mip]);
		for (uint i = 0; i < mip_size; i++, color++)
		{
			color->r = gammatable[color->r];
			color->g = gammatable[color->g];
			color->b = gammatable[color->b];
		}
	}
}

static void R_UploadM32(miptex32_t* mt, const image_t* img)
{
	int mip;
	for (mip = 0; mip < MIPLEVELS && mt->width[mip] > 0 && mt->height[mip] > 0; mip++)
		glTexImage2D(GL_TEXTURE_2D, mip, GL_RGBA, (int)mt->width[mip], (int)mt->height[mip], 0, GL_RGBA, GL_UNSIGNED_BYTE, (byte*)mt + mt->offsets[mip]);

	// GL 3.3 Core: set max mip level to avoid incomplete texture.
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, max(mip - 1, 0));

	R_SetFilter(img);
}

static image_t* R_LoadM32(const char* name, const imagetype_t type)
{
	miptex32_t* mt;

	ri.FS_LoadFile(name, (void**)&mt);
	if (mt == NULL)
	{
		ri.Con_Printf(PRINT_ALL, "R_LoadM32: can't load '%s'\n", name);
		return NULL;
	}

	if (mt->version != MIP32_VERSION)
	{
		ri.Con_Printf(PRINT_ALL, "R_LoadM32: can't load '%s': invalid version (%i)\n", name, mt->version);
		ri.FS_FreeFile(mt);

		return NULL;
	}

	if (strlen(name) >= MAX_QPATH)
	{
		ri.Con_Printf(PRINT_ALL, "R_LoadM32: can't load '%s': filename too long\n", name);
		ri.FS_FreeFile(mt);

		return NULL;
	}

	R_ApplyGamma32(mt);

	image_t* image = R_GetFreeImage();
	strcpy_s(image->name, sizeof(image->name), name);
	image->registration_sequence = registration_sequence;
	image->width = (int)mt->width[0];
	image->height = (int)mt->height[0];
	image->type = type;
	image->palette = NULL;
	image->has_alpha = 1;
	image->num_frames = (byte)mt->num_frames;

	glGenTextures(1, (GLuint*)&image->texnum);

	R_BindImage(image);
	R_UploadM32(mt, image);
	ri.FS_FreeFile(mt);

	return image;
}

#pragma endregion

#pragma region ========================== HD TEXTURE LOADING ==========================

#define HD_TEXTURES_DIR		"HDTextures"
#define MAX_HD_ENTRIES		4096
#define NUM_HD_HASH_BUCKETS	512

static char hd_root_dir[MAX_OSPATH]; // Absolute path to HDTextures/ root, used to compute relative paths.
static int hd_root_dir_len;

typedef struct hdentry_s
{
	struct hdentry_s* next;
	char rel_path[MAX_OSPATH]; // Relative path from HDTextures root, lowercased, no extension (e.g. "textures/castle/wall01").
	char full_path[MAX_OSPATH]; // Absolute filesystem path to the PNG file.
} hdentry_t;

static hdentry_t hd_entries[MAX_HD_ENTRIES];
static int num_hd_entries;
static hdentry_t* hd_hash[NUM_HD_HASH_BUCKETS];

// DJB2 hash for the HD lookup table.
static uint R_HDHash(const char* str)
{
	uint hash = 5381;
	for (int c = *str; c != 0; c = *++str)
		hash = ((hash << 5) + hash) + (byte)tolower(c);

	return hash % NUM_HD_HASH_BUCKETS;
}

// Copies path into out, lowercased, with separators normalized to '/' and extension stripped.
static void R_NormalizePath(const char* path, char* out, const int out_size)
{
	// Strip extension.
	const char* dot = strrchr(path, '.');
	const int path_len = (dot != NULL) ? (int)(dot - path) : (int)strlen(path);
	const int copy_len = min(path_len, out_size - 1);

	for (int i = 0; i < copy_len; i++)
	{
		const char c = path[i];
		out[i] = (c == '\\') ? '/' : (char)tolower((byte)c);
	}

	out[copy_len] = '\0';
}

// Recursively scans a directory for PNG files and registers them in the HD lookup table.
static void R_ScanHDDirectory(const char* dir_path)
{
	char search_path[MAX_OSPATH];
	struct _finddata_t findinfo;

	// Search for all entries in this directory.
	Com_sprintf(search_path, sizeof(search_path), "%s/*", dir_path);

	const intptr_t handle = _findfirst(search_path, &findinfo);
	if (handle == -1)
		return;

	do
	{
		// Skip "." and "..".
		if (strcmp(findinfo.name, ".") == 0 || strcmp(findinfo.name, "..") == 0)
			continue;

		char full_path[MAX_OSPATH];
		Com_sprintf(full_path, sizeof(full_path), "%s/%s", dir_path, findinfo.name);

		if (findinfo.attrib & _A_SUBDIR)
		{
			// Recurse into subdirectories.
			R_ScanHDDirectory(full_path);
		}
		else
		{
			// Check if it's a PNG file.
			const int name_len = (int)strlen(findinfo.name);
			if (name_len < 5 || Q_stricmp(findinfo.name + name_len - 4, ".png") != 0)
				continue;

			if (num_hd_entries >= MAX_HD_ENTRIES)
			{
				ri.Con_Printf(PRINT_ALL, "R_ScanHDDirectory: HD texture limit reached (%i)\n", MAX_HD_ENTRIES);
				_findclose(handle);
				return;
			}

			// Register this entry using relative path from HDTextures root.
			hdentry_t* entry = &hd_entries[num_hd_entries++];
			R_NormalizePath(full_path + hd_root_dir_len, entry->rel_path, sizeof(entry->rel_path));
			strcpy_s(entry->full_path, sizeof(entry->full_path), full_path);

			// Insert into hash table.
			const uint h = R_HDHash(entry->rel_path);
			entry->next = hd_hash[h];
			hd_hash[h] = entry;
		}
	} while (_findnext(handle, &findinfo) == 0);

	_findclose(handle);
}

// Looks up an HD replacement PNG by the relative path of the original texture.
// E.g. for "textures/castle/wall01.m8", looks for "textures/castle/wall01" in the hash table.
static const char* R_FindHDTexturePath(const char* name)
{
	char rel_path[MAX_OSPATH];
	R_NormalizePath(name, rel_path, sizeof(rel_path));

	const uint h = R_HDHash(rel_path);
	for (const hdentry_t* entry = hd_hash[h]; entry != NULL; entry = entry->next)
	{
		if (strcmp(entry->rel_path, rel_path) == 0)
			return entry->full_path;
	}

	return NULL;
}

static void R_InitHDTextures(void)
{
	num_hd_entries = 0;
	memset(hd_hash, 0, sizeof(hd_hash));

	Com_sprintf(hd_root_dir, sizeof(hd_root_dir), "%s/%s", ri.FS_Gamedir(), HD_TEXTURES_DIR);
	hd_root_dir_len = (int)strlen(hd_root_dir) + 1; // +1 to skip the '/' separator after the root.

	R_ScanHDDirectory(hd_root_dir);

	if (num_hd_entries > 0)
		ri.Con_Printf(PRINT_ALL, "Found %i HD replacement textures in '%s'\n", num_hd_entries, hd_root_dir);
}

static void R_ApplyGammaHD(byte* pixels, const int width, const int height, const int channels)
{
	const int size = width * height;
	for (int i = 0; i < size; i++)
	{
		pixels[i * channels + 0] = gammatable[pixels[i * channels + 0]];
		pixels[i * channels + 1] = gammatable[pixels[i * channels + 1]];
		pixels[i * channels + 2] = gammatable[pixels[i * channels + 2]];
	}
}

static void R_UploadHD(byte* pixels, const image_t* image)
{
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, image->hd_width, image->hd_height, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
	glGenerateMipmap(GL_TEXTURE_2D);

	// HD textures always use high-quality trilinear filtering.
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

	// Apply max anisotropic filtering if supported.
	if (gl_config.max_anisotropy > 0.0f)
		glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAX_ANISOTROPY_EXT, gl_config.max_anisotropy);
}

// Loads pixels from an absolute PNG file path using stdio.
static byte* R_LoadPNGFile(const char* abs_path, int* width, int* height)
{
	FILE* f;
	if (fopen_s(&f, abs_path, "rb") != 0 || f == NULL)
		return NULL;

	fseek(f, 0, SEEK_END);
	const int file_len = ftell(f);
	fseek(f, 0, SEEK_SET);

	byte* file_data = malloc(file_len);
	if (file_data == NULL)
	{
		fclose(f);
		return NULL;
	}

	fread(file_data, 1, file_len, f);
	fclose(f);

	int channels;
	byte* pixels = stbi_load_from_memory(file_data, file_len, width, height, &channels, 4);
	free(file_data);

	return pixels;
}

// Reads the original M8/M32 file header to get its texture dimensions.
// Returns false if the original file can't be read.
static qboolean R_GetOriginalTextureDims(const char* name, int* orig_w, int* orig_h)
{
	void* data;
	const int len = ri.FS_LoadFile(name, &data);

	if (data == NULL || len < (int)sizeof(int) * 3) // Need at least version + width + height
	{
		if (data != NULL)
			ri.FS_FreeFile(data);

		return false;
	}

	const int name_len = (int)strlen(name);

	if (name_len > 3 && Q_stricmp(name + name_len - 3, ".m8") == 0)
	{
		const miptex_t* mt = (const miptex_t*)data;
		if (mt->version == MIP_VERSION)
		{
			*orig_w = (int)mt->width[0];
			*orig_h = (int)mt->height[0];
			ri.FS_FreeFile(data);
			return true;
		}
	}
	else if (name_len > 4 && Q_stricmp(name + name_len - 4, ".m32") == 0)
	{
		const miptex32_t* mt = (const miptex32_t*)data;
		if (mt->version == MIP32_VERSION)
		{
			*orig_w = (int)mt->width[0];
			*orig_h = (int)mt->height[0];
			ri.FS_FreeFile(data);
			return true;
		}
	}

	ri.FS_FreeFile(data);
	return false;
}

// Tries to load an HD replacement PNG for the given texture name.
// Returns NULL if no replacement exists.
static image_t* R_LoadHDTexture(const char* name, const imagetype_t type)
{
	const char* hd_path = R_FindHDTexturePath(name);
	if (hd_path == NULL)
		return NULL;

	int hd_width, hd_height;
	byte* pixels = R_LoadPNGFile(hd_path, &hd_width, &hd_height);

	if (pixels == NULL)
	{
		ri.Con_Printf(PRINT_ALL, "R_LoadHDTexture: failed to decode '%s'\n", hd_path);
		return NULL;
	}

	// Get original texture dimensions for correct UV mapping.
	int orig_w, orig_h;
	if (!R_GetOriginalTextureDims(name, &orig_w, &orig_h))
	{
		// Fall back to HD dimensions if original can't be read.
		orig_w = hd_width;
		orig_h = hd_height;
	}

	R_ApplyGammaHD(pixels, hd_width, hd_height, 4);

	image_t* image = R_GetFreeImage();
	strcpy_s(image->name, sizeof(image->name), name);
	image->registration_sequence = registration_sequence;
	image->width = orig_w;
	image->height = orig_h;
	image->hd_width = hd_width;
	image->hd_height = hd_height;
	image->type = type;
	image->palette = NULL;
	image->has_alpha = true;
	image->is_hd = true;
	image->num_frames = 0;

	// Store a copy of the resolved path for gamma reload.
	const int path_len = (int)strlen(hd_path) + 1;
	image->hd_path = malloc(path_len);
	strcpy_s(image->hd_path, path_len, hd_path);

	glGenTextures(1, (GLuint*)&image->texnum);

	R_BindImage(image);
	R_UploadHD(pixels, image);
	stbi_image_free(pixels);

	ri.Con_Printf(PRINT_DEVELOPER, "R_LoadHDTexture: '%s' -> '%s'\n", name, hd_path);

	return image;
}

// Reloads an HD texture from disk and reapplies gamma.
static void R_ReloadHDTexture(image_t* image)
{
	if (image->hd_path == NULL)
		return;

	int width, height;
	byte* pixels = R_LoadPNGFile(image->hd_path, &width, &height);
	if (pixels == NULL)
		return;

	R_ApplyGammaHD(pixels, width, height, 4);
	R_BindImage(image);
	R_UploadHD(pixels, image);
	stbi_image_free(pixels);
}

#pragma endregion

image_t* R_FindImage(const char* name, const imagetype_t type)
{
	if (name == NULL)
	{
		ri.Con_Printf(PRINT_ALL, "R_FindImage: Invalid null name\n");
		return r_notexture;
	}

	const uint len = strlen(name);

	if (len < 8)
	{
		ri.Con_Printf(PRINT_ALL, "R_FindImage: Name too short (%s)\n", name);
		return r_notexture;
	}

	const byte hash = name[len - 7] + name[len - 5] * name[len - 6];
	image_t* image = gltextures_hashed[hash];

	while (image != NULL)
	{
		if (strcmp(name, image->name) == 0)
		{
			image->registration_sequence = registration_sequence;
			return image;
		}

		image = image->next;
	}

	// Try HD texture replacement first.
	image = R_LoadHDTexture(name, type);

	if (image == NULL)
	{
		if (strcmp(name + len - 3, ".m8") == 0)
			image = R_LoadM8(name, type);
		else if (strcmp(name + len - 4, ".m32") == 0)
			image = R_LoadM32(name, type);
		else
			ri.Con_Printf(PRINT_ALL, "R_FindImage: Extension not recognized in '%s'\n", name);
	}

	if (image == NULL)
		return r_notexture;

	image->next = gltextures_hashed[hash];
	gltextures_hashed[hash] = image;

	return image;
}

struct image_s* RI_RegisterSkin(const char* name, qboolean* retval)
{
	image_t* img = R_FindImage(name, it_skin);
	if (retval != NULL)
		*retval = (img != r_notexture);

	return img;
}

static void R_FreeImage(image_t* image)
{
	glDeleteTextures(1, (GLuint*)&image->texnum);
	if (image->palette != NULL)
	{
		free(image->palette);
		image->palette = NULL;
	}

	if (image->hd_path != NULL)
	{
		free(image->hd_path);
		image->hd_path = NULL;
	}

	const uint len = strlen(image->name);
	const byte hash = image->name[len - 7] + image->name[len - 5] * image->name[len - 6];

	image_t** tgt = &gltextures_hashed[hash];
	for (image_t* img = gltextures_hashed[hash]; img != image; img = img->next)
		tgt = &img->next;

	*tgt = image->next;
	image->registration_sequence = 0;

	if (gl_state.currenttextures[gl_state.currenttmu] == image->texnum)
		gl_state.currenttextures[gl_state.currenttmu] = -1;
}

void R_FreeImageNoHash(image_t* image)
{
	glDeleteTextures(1, (GLuint*)&image->texnum);
	if (image->palette != NULL)
	{
		free(image->palette);
		image->palette = NULL;
	}

	if (image->hd_path != NULL)
	{
		free(image->hd_path);
		image->hd_path = NULL;
	}

	image->registration_sequence = 0;

	if (gl_state.currenttextures[gl_state.currenttmu] == image->texnum)
		gl_state.currenttextures[gl_state.currenttmu] = -1;
}

void R_FreeUnusedImages(void)
{
	// Make sure permanent/global textures are never freed.
	r_notexture->registration_sequence = registration_sequence;
	r_particletexture->registration_sequence = registration_sequence;
	r_aparticletexture->registration_sequence = registration_sequence;
	r_reflecttexture->registration_sequence = registration_sequence;
	draw_chars->registration_sequence = registration_sequence;
	r_font1->registration_sequence = registration_sequence;
	r_font2->registration_sequence = registration_sequence;

	image_t* image = &gltextures[0];
	for (int i = 0; i < numgltextures; i++, image++)
	{
		if (image->registration_sequence == registration_sequence)
			continue;
		if (image->registration_sequence == 0)
			continue;

		R_FreeImage(image);
	}
}

void R_GammaAffect(const qboolean refresh_all)
{
	R_InitGammaTable();

	image_t* image = &gltextures[0];
	for (int i = 0; i < numgltextures; i++, image++)
	{
		if (image->registration_sequence == 0)
			continue;

		if (!refresh_all && image->registration_sequence != registration_sequence)
			continue;

		if (image->is_hd)
		{
			// Reload HD PNG from disk.
			R_ReloadHDTexture(image);
		}
		else if (image->palette != NULL)
		{
			// Reload M8 from disk.
			miptex_t* mt;
			ri.FS_LoadFile(image->name, (void**)&mt);
			if (mt != NULL && mt->version == MIP_VERSION)
			{
				GrabPalette(mt->palette, image->palette);
				FixPalette(image);
				R_BindImage(image);
				R_UploadM8(mt, image);
				ri.FS_FreeFile(mt);
			}
		}
		else if (image->has_alpha)
		{
			// Reload M32 from disk.
			miptex32_t* mt;
			ri.FS_LoadFile(image->name, (void**)&mt);
			if (mt != NULL && mt->version == MIP32_VERSION)
			{
				R_ApplyGamma32(mt);
				R_BindImage(image);
				R_UploadM32(mt, image);
				ri.FS_FreeFile(mt);
			}
		}
	}
}

void R_InitImages(void)
{
	registration_sequence = 1;
	gl_state.inverse_intensity = 1.0f;

	numgltextures = 0;
	memset(gltextures, 0, sizeof(gltextures));
	memset(gltextures_hashed, 0, sizeof(gltextures_hashed));

	if (upload_buffer != NULL)
	{
		free(upload_buffer);
		upload_buffer = NULL;
		upload_buffer_size = 0;
	}

	R_InitHDTextures();
	R_InitMinlight();
}

void R_ShutdownImages(void)
{
	image_t* image = &gltextures[0];
	for (int i = 0; i < numgltextures; i++, image++)
	{
		if (image->registration_sequence == 0)
			continue;

		glDeleteTextures(1, (GLuint*)&image->texnum);
		if (image->palette != NULL)
		{
			free(image->palette);
			image->palette = NULL;
		}

		if (image->hd_path != NULL)
		{
			free(image->hd_path);
			image->hd_path = NULL;
		}
	}

	numgltextures = 0;
	memset(gltextures, 0, sizeof(gltextures));
	memset(gltextures_hashed, 0, sizeof(gltextures_hashed));

	if (upload_buffer != NULL)
	{
		free(upload_buffer);
		upload_buffer = NULL;
		upload_buffer_size = 0;
	}
}
