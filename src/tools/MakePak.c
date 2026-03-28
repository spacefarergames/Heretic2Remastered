//
// MakePak.c
//
// Standalone tool to pack a directory tree into a PAK2 file (extended PAK format).
//
// Usage: MakePak.exe <inputdir> [outputfile]
//   inputdir:   Path to the directory to pack (e.g. "C:\Games\Heretic2\base").
//   outputfile: Output .pak filename (default: "base.pak" in the current directory).
//
// The PAK2 format (header ident "PAK2" instead of "PACK"):
//   - Header:    12 bytes (int ident, int dirofs, int dirlen)
//   - File data: concatenated raw file contents
//   - Directory: array of { char name[128]; int filepos; int filelen; }
//

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <direct.h>
#include <io.h>

#pragma region ========================== PAK2 format definitions ==========================

typedef unsigned char byte;
typedef unsigned int uint;

// Extended PAK format with 128-char filenames.
#define IDPAK2HEADER	(('2' << 24) + ('K' << 16) + ('A' << 8) + 'P')
#define MAX_FILES_IN_PACK	6144

typedef struct
{
	char name[128];
	int filepos;
	int filelen;
} dpackfile2_t;

typedef struct
{
	int ident;
	int dirofs;
	int dirlen;
} dpackheader_t;

#pragma endregion

#pragma region ========================== File collection ==========================

typedef struct
{
	char relative_path[256]; // Forward-slash relative path for PAK entry.
	char full_path[512];     // Full OS path for reading.
} fileentry_t;

static fileentry_t g_files[MAX_FILES_IN_PACK];
static int g_numfiles = 0;

// Normalize path separators to forward slashes.
static void NormalizePath(char* path)
{
	for (char* p = path; *p; p++)
		if (*p == '\\')
			*p = '/';
}

// Remove trailing slashes.
static void StripTrailingSlash(char* path)
{
	int len = (int)strlen(path);
	while (len > 0 && (path[len - 1] == '/' || path[len - 1] == '\\'))
		path[--len] = '\0';
}

// Recursively collect files from a directory.
// basedir: the root input directory (for computing relative paths).
// subdir:  current subdirectory relative to basedir (empty string for root).
static void CollectFiles(const char* basedir, const char* subdir)
{
	char searchpath[512];
	struct _finddata_t finddata;
	intptr_t handle;

	if (subdir[0] != '\0')
		sprintf_s(searchpath, sizeof(searchpath), "%s/%s/*", basedir, subdir);
	else
		sprintf_s(searchpath, sizeof(searchpath), "%s/*", basedir);

	handle = _findfirst(searchpath, &finddata);
	if (handle == -1)
		return;

	do
	{
		// Skip . and ..
		if (strcmp(finddata.name, ".") == 0 || strcmp(finddata.name, "..") == 0)
			continue;

		char relpath[256];
		if (subdir[0] != '\0')
			sprintf_s(relpath, sizeof(relpath), "%s/%s", subdir, finddata.name);
		else
			sprintf_s(relpath, sizeof(relpath), "%s", finddata.name);

		if (finddata.attrib & _A_SUBDIR)
		{
			// Recurse into subdirectory.
			CollectFiles(basedir, relpath);
		}
		else
		{
			// Skip .cfg, .dll, and .pak files.
			const char* ext = strrchr(finddata.name, '.');
			if (ext != NULL && (_stricmp(ext, ".cfg") == 0 || _stricmp(ext, ".dll") == 0 || _stricmp(ext, ".pak") == 0))
				continue;
			if (g_numfiles >= MAX_FILES_IN_PACK)
			{
				fprintf(stderr, "ERROR: Too many files (max %d).\n", MAX_FILES_IN_PACK);
				_findclose(handle);
				return;
			}

			// Store relative path (forward slashes).
			strcpy_s(g_files[g_numfiles].relative_path, sizeof(g_files[g_numfiles].relative_path), relpath);
			NormalizePath(g_files[g_numfiles].relative_path);

			// Store full path.
			sprintf_s(g_files[g_numfiles].full_path, sizeof(g_files[g_numfiles].full_path), "%s/%s", basedir, relpath);
			NormalizePath(g_files[g_numfiles].full_path);

			// Validate name length for PAK2 format (max 127 chars + null).
			if (strlen(g_files[g_numfiles].relative_path) >= 128)
			{
				fprintf(stderr, "WARNING: Skipping '%s' (path too long for PAK2 format, max 127 chars).\n",
					g_files[g_numfiles].relative_path);
				continue;
			}

			g_numfiles++;
		}
	} while (_findnext(handle, &finddata) == 0);

	_findclose(handle);
}

#pragma endregion

#pragma region ========================== PAK writing ==========================

static int WritePak(const char* outputfile)
{
	FILE* pakfile;

	if (fopen_s(&pakfile, outputfile, "wb") != 0)
	{
		fprintf(stderr, "ERROR: Cannot create output file '%s'.\n", outputfile);
		return 1;
	}

	// Write placeholder header (will be updated later).
	dpackheader_t header;
	header.ident = IDPAK2HEADER;
	header.dirofs = 0;
	header.dirlen = 0;
	fwrite(&header, sizeof(header), 1, pakfile);

	// Write file data and record positions.
	dpackfile2_t directory[MAX_FILES_IN_PACK];
	static byte buffer[65536];

	for (int i = 0; i < g_numfiles; i++)
	{
		FILE* infile;
		if (fopen_s(&infile, g_files[i].full_path, "rb") != 0)
		{
			fprintf(stderr, "WARNING: Cannot read '%s', skipping.\n", g_files[i].full_path);

			// Remove this entry by shifting remaining entries.
			for (int j = i; j < g_numfiles - 1; j++)
				g_files[j] = g_files[j + 1];

			g_numfiles--;
			i--;
			continue;
		}

		// Record directory entry.
		memset(&directory[i], 0, sizeof(dpackfile2_t));
		strcpy_s(directory[i].name, sizeof(directory[i].name), g_files[i].relative_path);
		directory[i].filepos = (int)ftell(pakfile);

		// Copy file contents.
		int total = 0;
		while (1)
		{
			const size_t bytesread = fread(buffer, 1, sizeof(buffer), infile);
			if (bytesread == 0)
				break;

			fwrite(buffer, 1, bytesread, pakfile);
			total += (int)bytesread;
		}

		directory[i].filelen = total;
		fclose(infile);
	}

	// Write directory.
	header.dirofs = (int)ftell(pakfile);
	header.dirlen = g_numfiles * (int)sizeof(dpackfile2_t);
	fwrite(directory, sizeof(dpackfile2_t), g_numfiles, pakfile);

	// Update header.
	fseek(pakfile, 0, SEEK_SET);
	fwrite(&header, sizeof(header), 1, pakfile);

	fclose(pakfile);

	return 0;
}

#pragma endregion

#pragma region ========================== Main ==========================

int main(int argc, char* argv[])
{
	printf("MakePak - PAK2 file packer (extended 128-char filenames)\n\n");

	if (argc < 2)
	{
		printf("Usage: MakePak.exe <inputdir> [outputfile]\n");
		printf("  inputdir:   Directory to pack.\n");
		printf("  outputfile: Output .pak filename (default: base.pak).\n");
		return 1;
	}

	// Get input directory.
	char inputdir[512];
	strcpy_s(inputdir, sizeof(inputdir), argv[1]);
	NormalizePath(inputdir);
	StripTrailingSlash(inputdir);

	// Get output filename.
	const char* outputfile = (argc >= 3) ? argv[2] : "base.pak";

	printf("Input directory: %s\n", inputdir);
	printf("Output file:     %s\n\n", outputfile);

	// Collect files.
	printf("Scanning directory...\n");
	CollectFiles(inputdir, "");

	if (g_numfiles == 0)
	{
		fprintf(stderr, "ERROR: No files found in '%s'.\n", inputdir);
		return 1;
	}

	printf("Found %d files.\n\n", g_numfiles);

	// Write PAK.
	printf("Writing %s...\n", outputfile);
	const int result = WritePak(outputfile);

	if (result == 0)
		printf("Done! Packed %d files into %s.\n", g_numfiles, outputfile);

	return result;
}

#pragma endregion
