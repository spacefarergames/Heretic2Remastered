//
// DumpTextures.c
//
// Standalone tool to extract all .m8 and .m32 textures from Heretic II PAK files
// and loose game directories (models, pics, players, skins) and save them as PNGs
// for HD texture replacement.
//
// Usage: DumpTextures.exe [basedir]
//   basedir: Path to the Heretic II game directory (e.g. "C:\Games\Heretic2\base").
//            Defaults to the current working directory if not specified.
//
// Output: PNG files are written to a "DumpedTextures" folder next to the PAK files,
//         preserving the original directory structure.
//         e.g. textures/castle/wall01.m8  -> DumpedTextures/textures/castle/wall01.png
//         e.g. models/weapon/weapon.m32   -> DumpedTextures/models/weapon/weapon.png
//         e.g. players/corvus/corvus.m32  -> DumpedTextures/players/corvus/corvus.png
//

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <direct.h>
#include <sys/stat.h>
#include <io.h>

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb/stb_image_write.h>

#pragma region ========================== Format definitions ==========================

// From q_Typedef.h
typedef unsigned char byte;
typedef unsigned int uint;

// From q_shared.h
typedef struct
{
	byte r;
	byte g;
	byte b;
} paletteRGB_t;

typedef struct
{
	byte r;
	byte g;
	byte b;
	byte a;
} paletteRGBA_t;

// From qfiles.h
#define IDPAKHEADER			(('K' << 24) + ('C' << 16) + ('A' << 8) + 'P')
#define MAX_FILES_IN_PACK	6144
#define MIPLEVELS			16
#define MIP_VERSION			2
#define MIP32_VERSION		4
#define PAL_SIZE			256
#define MAX_OSPATH			260
#define MAX_QPATH			64

typedef struct
{
	char name[56];
	int filepos;
	int filelen;
} dpackfile_t;

typedef struct
{
	int ident;
	int dirofs;
	int dirlen;
} dpackheader_t;

typedef struct
{
	int version;
	char name[32];
	uint width[MIPLEVELS];
	uint height[MIPLEVELS];
	uint offsets[MIPLEVELS];
	char animname[32];
	paletteRGB_t palette[PAL_SIZE];
	int flags;
	int contents;
	int value;
} miptex_t;

typedef struct
{
	int version;
	char name[128];
	char altname[128];
	char animname[128];
	char damagename[128];
	uint width[MIPLEVELS];
	uint height[MIPLEVELS];
	uint offsets[MIPLEVELS];
	int flags;
	int contents;
	int num_frames;
	float scale_x;
	float scale_y;
	int mip_scale;

	char dt_name[128];
	float dt_scale_x;
	float dt_scale_y;
	float dt_u;
	float dt_v;
	float dt_alpha;
	int dt_src_blend_mode;
	int dt_dst_blend_mode;

	int unused[20];
} miptex32_t;

#pragma endregion

static int textures_dumped = 0;
static int textures_failed = 0;

// Creates all directories in the path (like mkdir -p).
static void MakePath(const char* path)
{
	char tmp[MAX_OSPATH];
	strncpy_s(tmp, sizeof(tmp), path, _TRUNCATE);

	for (char* p = tmp + 1; *p != '\0'; p++)
	{
		if (*p == '/' || *p == '\\')
		{
			*p = '\0';
			_mkdir(tmp);
			*p = '/';
		}
	}
}

// Builds the output PNG path from the texture's internal name.
// "textures/castle/wall01.m8" -> "<outdir>/textures/castle/wall01.png"
static void BuildOutputPath(const char* out_dir, const char* tex_name, char* out_path, int out_path_size)
{
	// Strip extension from texture name.
	char base[MAX_OSPATH];
	strncpy_s(base, sizeof(base), tex_name, _TRUNCATE);

	// Normalize separators.
	for (char* p = base; *p; p++)
		if (*p == '\\') *p = '/';

	char* dot = strrchr(base, '.');
	if (dot != NULL)
		*dot = '\0';

	snprintf(out_path, out_path_size, "%s/%s.png", out_dir, base);

	// Normalize output path separators.
	for (char* p = out_path; *p; p++)
		if (*p == '\\') *p = '/';
}

static void DumpM8Data(const byte* data, int data_len, const char* tex_name, const char* out_dir)
{
	if (data == NULL || data_len < (int)sizeof(miptex_t))
		return;

	const miptex_t* mt = (const miptex_t*)data;

	if (mt->version != MIP_VERSION)
	{
		printf("  SKIP %s (bad version %d)\n", tex_name, mt->version);
		textures_failed++;
		return;
	}

	const uint w = mt->width[0];
	const uint h = mt->height[0];
	if (w == 0 || h == 0 || mt->offsets[0] == 0)
	{
		printf("  SKIP %s (zero dimensions)\n", tex_name);
		textures_failed++;
		return;
	}

	// Convert paletted mip0 to RGBA.
	const uint pixel_count = w * h;
	byte* rgba = malloc(pixel_count * 4);
	if (rgba == NULL)
		return;

	const byte* src = data + mt->offsets[0];
	for (uint i = 0; i < pixel_count; i++)
	{
		const paletteRGB_t* c = &mt->palette[src[i]];
		rgba[i * 4 + 0] = c->r;
		rgba[i * 4 + 1] = c->g;
		rgba[i * 4 + 2] = c->b;
		rgba[i * 4 + 3] = 255;
	}

	// Build output path and save.
	char out_path[MAX_OSPATH];
	BuildOutputPath(out_dir, tex_name, out_path, sizeof(out_path));
	MakePath(out_path);

	if (stbi_write_png(out_path, (int)w, (int)h, 4, rgba, (int)(w * 4)))
	{
		printf("  OK   %s (%ux%u)\n", tex_name, w, h);
		textures_dumped++;
	}
	else
	{
		printf("  FAIL %s (write error)\n", tex_name);
		textures_failed++;
	}

	free(rgba);
}

static void DumpM8(FILE* pak, const dpackfile_t* entry, const char* out_dir)
{
	byte* data = malloc(entry->filelen);
	if (data == NULL)
		return;

	fseek(pak, entry->filepos, SEEK_SET);
	if (fread(data, 1, entry->filelen, pak) != (size_t)entry->filelen)
	{
		free(data);
		return;
	}

	DumpM8Data(data, entry->filelen, entry->name, out_dir);
	free(data);
}

static void DumpM32Data(const byte* data, int data_len, const char* tex_name, const char* out_dir)
{
	if (data == NULL || data_len < (int)sizeof(miptex32_t))
		return;

	const miptex32_t* mt = (const miptex32_t*)data;

	if (mt->version != MIP32_VERSION)
	{
		printf("  SKIP %s (bad version %d)\n", tex_name, mt->version);
		textures_failed++;
		return;
	}

	const uint w = mt->width[0];
	const uint h = mt->height[0];
	if (w == 0 || h == 0 || mt->offsets[0] == 0)
	{
		printf("  SKIP %s (zero dimensions)\n", tex_name);
		textures_failed++;
		return;
	}

	// M32 mip0 data is already RGBA.
	const byte* src = data + mt->offsets[0];

	char out_path[MAX_OSPATH];
	BuildOutputPath(out_dir, tex_name, out_path, sizeof(out_path));
	MakePath(out_path);

	if (stbi_write_png(out_path, (int)w, (int)h, 4, src, (int)(w * 4)))
	{
		printf("  OK   %s (%ux%u)\n", tex_name, w, h);
		textures_dumped++;
	}
	else
	{
		printf("  FAIL %s (write error)\n", tex_name);
		textures_failed++;
	}
}

static void DumpM32(FILE* pak, const dpackfile_t* entry, const char* out_dir)
{
	byte* data = malloc(entry->filelen);
	if (data == NULL)
		return;

	fseek(pak, entry->filepos, SEEK_SET);
	if (fread(data, 1, entry->filelen, pak) != (size_t)entry->filelen)
	{
		free(data);
		return;
	}

	DumpM32Data(data, entry->filelen, entry->name, out_dir);
	free(data);
}

static void ScanLooseDirectory(const char* abs_dir, const char* rel_prefix, const char* out_dir)
{
	char search_path[MAX_OSPATH];
	struct _finddata_t findinfo;

	snprintf(search_path, sizeof(search_path), "%s/*", abs_dir);

	const intptr_t handle = _findfirst(search_path, &findinfo);
	if (handle == -1)
		return;

	do
	{
		if (strcmp(findinfo.name, ".") == 0 || strcmp(findinfo.name, "..") == 0)
			continue;

		char abs_path[MAX_OSPATH];
		snprintf(abs_path, sizeof(abs_path), "%s/%s", abs_dir, findinfo.name);

		char rel_path[MAX_OSPATH];
		if (rel_prefix[0] != '\0')
			snprintf(rel_path, sizeof(rel_path), "%s/%s", rel_prefix, findinfo.name);
		else
			snprintf(rel_path, sizeof(rel_path), "%s", findinfo.name);

		if (findinfo.attrib & _A_SUBDIR)
		{
			ScanLooseDirectory(abs_path, rel_path, out_dir);
		}
		else
		{
			const int name_len = (int)strlen(findinfo.name);

			if (name_len > 3 && _stricmp(findinfo.name + name_len - 3, ".m8") == 0)
			{
				FILE* f;
				if (fopen_s(&f, abs_path, "rb") == 0 && f != NULL)
				{
					fseek(f, 0, SEEK_END);
					const long file_len = ftell(f);
					fseek(f, 0, SEEK_SET);
					byte* data = malloc(file_len);
					if (data != NULL)
					{
						fread(data, 1, file_len, f);
						DumpM8Data(data, (int)file_len, rel_path, out_dir);
						free(data);
					}
					fclose(f);
				}
			}
			else if (name_len > 4 && _stricmp(findinfo.name + name_len - 4, ".m32") == 0)
			{
				FILE* f;
				if (fopen_s(&f, abs_path, "rb") == 0 && f != NULL)
				{
					fseek(f, 0, SEEK_END);
					const long file_len = ftell(f);
					fseek(f, 0, SEEK_SET);
					byte* data = malloc(file_len);
					if (data != NULL)
					{
						fread(data, 1, file_len, f);
						DumpM32Data(data, (int)file_len, rel_path, out_dir);
						free(data);
					}
					fclose(f);
				}
			}
		}
	} while (_findnext(handle, &findinfo) == 0);

	_findclose(handle);
}

static void ProcessLooseDir(const char* gamedir, const char* subdir_name, const char* out_dir)
{
	char abs_dir[MAX_OSPATH];
	snprintf(abs_dir, sizeof(abs_dir), "%s/%s", gamedir, subdir_name);

	struct _stat st;
	if (_stat(abs_dir, &st) != 0 || !(st.st_mode & _S_IFDIR))
		return;

	printf("\n--- %s/ (loose files) ---\n", abs_dir);

	const int before = textures_dumped + textures_failed;
	ScanLooseDirectory(abs_dir, subdir_name, out_dir);

	if (textures_dumped + textures_failed == before)
		printf("  (no textures found)\n");
}

static void ProcessPak(const char* pak_path, const char* out_dir)
{
	FILE* f;
	if (fopen_s(&f, pak_path, "rb") != 0 || f == NULL)
		return; // PAK doesn't exist, skip silently.

	dpackheader_t header;
	if (fread(&header, sizeof(header), 1, f) != 1 || header.ident != IDPAKHEADER)
	{
		printf("WARNING: %s is not a valid PAK file\n", pak_path);
		fclose(f);
		return;
	}

	const int num_files = header.dirlen / (int)sizeof(dpackfile_t);
	if (num_files < 1 || num_files > MAX_FILES_IN_PACK)
	{
		printf("WARNING: %s has invalid file count (%d)\n", pak_path, num_files);
		fclose(f);
		return;
	}

	printf("\n--- %s (%d entries) ---\n", pak_path, num_files);

	// Read directory.
	dpackfile_t* dir = malloc(num_files * sizeof(dpackfile_t));
	if (dir == NULL)
	{
		fclose(f);
		return;
	}

	fseek(f, header.dirofs, SEEK_SET);
	fread(dir, sizeof(dpackfile_t), num_files, f);

	int tex_count = 0;
	for (int i = 0; i < num_files; i++)
	{
		const int name_len = (int)strlen(dir[i].name);

		if (name_len > 3 && _stricmp(dir[i].name + name_len - 3, ".m8") == 0)
		{
			DumpM8(f, &dir[i], out_dir);
			tex_count++;
		}
		else if (name_len > 4 && _stricmp(dir[i].name + name_len - 4, ".m32") == 0)
		{
			DumpM32(f, &dir[i], out_dir);
			tex_count++;
		}
	}

	if (tex_count == 0)
		printf("  (no textures found)\n");

	free(dir);
	fclose(f);
}

int main(int argc, char* argv[])
{
	printf("=== Heretic II PAK Texture Dumper ===\n\n");

	// Determine base directory.
	const char* basedir;
	char cwd[MAX_OSPATH];

	if (argc > 1)
	{
		basedir = argv[1];
	}
	else
	{
		_getcwd(cwd, sizeof(cwd));
		basedir = cwd;
	}

	printf("Base directory: %s\n", basedir);

	// Build game data directory path (basedir/base).
	char gamedir[MAX_OSPATH];
	snprintf(gamedir, sizeof(gamedir), "%s/base", basedir);

	// Build output directory path.
	char out_dir[MAX_OSPATH];
	snprintf(out_dir, sizeof(out_dir), "%s/DumpedTextures", basedir);
	_mkdir(out_dir);

	printf("Game directory: %s\n", gamedir);
	printf("Output directory: %s\n", out_dir);

	// Process Htic2-0.pak through Htic2-9.pak.
	for (int i = 0; i < 10; i++)
	{
		char pak_path[MAX_OSPATH];
		snprintf(pak_path, sizeof(pak_path), "%s/Htic2-%d.pak", gamedir, i);
		ProcessPak(pak_path, out_dir);
	}

	// Process loose textures from known game subdirectories.
	const char* loose_dirs[] = { "models", "pics", "players", "skins" };
	for (int i = 0; i < 4; i++)
		ProcessLooseDir(gamedir, loose_dirs[i], out_dir);

	printf("\n=== Done! Dumped %d textures (%d failed) ===\n", textures_dumped, textures_failed);
	printf("Output: %s\n\n", out_dir);
	printf("To use for HD replacement:\n");
	printf("  1. Upscale the PNGs with your preferred AI upscaler\n");
	printf("  2. Copy the upscaled files into: %s/base/HDTextures/\n", basedir);
	printf("     preserving the directory structure (textures are matched by path)\n");
	printf("     e.g. DumpedTextures/textures/castle/wall01.png\n");
	printf("      ->  base/HDTextures/textures/castle/wall01.png\n");
	printf("     e.g. DumpedTextures/models/weapon/weapon.png\n");
	printf("      ->  base/HDTextures/models/weapon/weapon.png\n");
	printf("     e.g. DumpedTextures/players/corvus/corvus.png\n");
	printf("      ->  base/HDTextures/players/corvus/corvus.png\n");

	return (textures_failed > 0) ? 1 : 0;
}
