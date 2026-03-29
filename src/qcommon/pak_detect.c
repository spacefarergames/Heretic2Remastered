//
// pak_detect.c -- Automatic PAK file detection from Steam/GOG/Program Files
//
// Searches common Heretic II installation locations and copies PAK files if found.
//

#include <windows.h>
#include <stdio.h>
#include <string.h>
#include "qcommon.h"

#pragma region ========================== Registry Utilities ==========================

// Query Windows Registry for a value
static qboolean Reg_QueryString(HKEY hive, const char* path, const char* value, char* buffer, int buffer_size)
{
	HKEY hkey;
	if (RegOpenKeyExA(hive, path, 0, KEY_READ, &hkey) != ERROR_SUCCESS)
		return false;

	DWORD size = buffer_size;
	qboolean success = (RegQueryValueExA(hkey, value, NULL, NULL, (LPBYTE)buffer, &size) == ERROR_SUCCESS);
	RegCloseKey(hkey);

	return success;
}

#pragma endregion

#pragma region ========================== Search Paths ==========================

// Possible Heretic II installation paths to search
static struct
{
	const char* registry_path;
	const char* registry_value;
	const char* fallback_path;
	qboolean check_registry;
} search_paths[] = {
	// Steam
	{ "SOFTWARE\\Valve\\Steam", "SteamPath", NULL, true },
	
	// GOG
	{ "SOFTWARE\\WOW6432Node\\GOG.com\\Games\\1444149254", "path", NULL, true },
	{ "SOFTWARE\\GOG.com\\Games\\1444149254", "path", NULL, true },
	
	// Program Files (check common locations)
	{ NULL, NULL, "C:\\Program Files\\Heretic II", false },
	{ NULL, NULL, "C:\\Program Files (x86)\\Heretic II", false },
	{ NULL, NULL, "C:\\Games\\Heretic II", false },
	{ NULL, NULL, "D:\\Games\\Heretic II", false },
};

#pragma endregion

#pragma region ========================== PAK File Operations ==========================

// Check if a file exists
static qboolean FileExists(const char* path)
{
	return GetFileAttributesA(path) != INVALID_FILE_ATTRIBUTES;
}

// Copy a file, creating directories if needed
static qboolean CopyPakFile(const char* src, const char* dst)
{
	if (!FileExists(src))
		return false;

	// Create destination directory if needed
	char dir[MAX_OSPATH];
	strcpy_s(dir, sizeof(dir), dst);
	char* slash = strrchr(dir, '\\');
	if (slash != NULL)
	{
		*slash = '\0';
		CreateDirectoryA(dir, NULL); // Returns false if already exists, that's ok
	}

	// Copy the file
	if (!CopyFileA(src, dst, FALSE)) // FALSE = overwrite if exists
	{
		Com_Printf("Warning: Failed to copy PAK file from %s to %s\n", src, dst);
		return false;
	}

	Com_Printf("Copied PAK file from %s to %s\n", src, dst);
	return true;
}

// Search for PAK files in a directory and copy them
static qboolean SearchAndCopyPaksFromDir(const char* game_dir, const char* base_dir)
{
	if (!FileExists(game_dir))
		return false;

	char search_path[MAX_OSPATH];
	Com_sprintf(search_path, sizeof(search_path), "%s\\*", game_dir);

	WIN32_FIND_DATAA find_data;
	HANDLE find_handle = FindFirstFileA(search_path, &find_data);
	if (find_handle == INVALID_HANDLE_VALUE)
		return false;

	qboolean found_pak0 = false;
	qboolean found_pak1 = false;
	qboolean copied_any = false;

	do
	{
		const char* filename = find_data.cFileName;
		
		// Check for Htic2-0.pak and Htic2-1.pak
		if (_stricmp(filename, "Htic2-0.pak") == 0)
		{
			char src[MAX_OSPATH], dst[MAX_OSPATH];
			Com_sprintf(src, sizeof(src), "%s\\%s", game_dir, filename);
			Com_sprintf(dst, sizeof(dst), "%s\\%s", base_dir, filename);

			if (!FileExists(dst)) // Only copy if not already present
			{
				if (CopyPakFile(src, dst))
					copied_any = true;
			}
			found_pak0 = true;
		}
		else if (_stricmp(filename, "Htic2-1.pak") == 0)
		{
			char src[MAX_OSPATH], dst[MAX_OSPATH];
			Com_sprintf(src, sizeof(src), "%s\\%s", game_dir, filename);
			Com_sprintf(dst, sizeof(dst), "%s\\%s", base_dir, filename);

			if (!FileExists(dst)) // Only copy if not already present
			{
				if (CopyPakFile(src, dst))
					copied_any = true;
			}
			found_pak1 = true;
		}

	} while (FindNextFileA(find_handle, &find_data));

	FindClose(find_handle);

	// Return true if we found both files (even if we didn't need to copy)
	return found_pak0 && found_pak1;
}

#pragma endregion

#pragma region ========================== Main PAK Detection ==========================

qboolean CD_SearchAndCopyPaks(const char* base_dir)
{
	Com_Printf("Searching for Heretic II PAK files...\n");

	// Search through all possible paths
	for (size_t i = 0; i < sizeof(search_paths) / sizeof(search_paths[0]); i++)
	{
		const char* registry_path = search_paths[i].registry_path;
		const char* registry_value = search_paths[i].registry_value;
		const char* fallback_path = search_paths[i].fallback_path;

		char install_dir[MAX_OSPATH];
		qboolean found = false;

		if (search_paths[i].check_registry && registry_path != NULL)
		{
			// Try to get path from Registry
			if (Reg_QueryString(HKEY_LOCAL_MACHINE, registry_path, registry_value, install_dir, sizeof(install_dir)))
			{
				found = true;
				Com_Printf("  Found Heretic II installation via Registry: %s\n", install_dir);
			}
		}
		else if (fallback_path != NULL)
		{
			// Try fallback path
			strcpy_s(install_dir, sizeof(install_dir), fallback_path);
			Com_Printf("  Checking common location: %s\n", install_dir);
			found = FileExists(install_dir);
		}

		if (found)
		{
			// Check for PAK files in Steam directory
			if (search_paths[i].check_registry && registry_path != NULL)
			{
				// Steam: Htic2 is usually in SteamPath\steamapps\common\Heretic 2
				char steam_h2_path[MAX_OSPATH];
				Com_sprintf(steam_h2_path, sizeof(steam_h2_path), "%s\\steamapps\\common\\Heretic 2", install_dir);
				if (SearchAndCopyPaksFromDir(steam_h2_path, base_dir))
					return true;

				// Also check just the install dir
				if (SearchAndCopyPaksFromDir(install_dir, base_dir))
					return true;
			}
			else
			{
				// GOG or Program Files: check directly
				if (SearchAndCopyPaksFromDir(install_dir, base_dir))
					return true;
			}
		}
	}

	Com_Printf("No Heretic II PAK files found in common locations.\n");
	return false;
}

#pragma endregion
