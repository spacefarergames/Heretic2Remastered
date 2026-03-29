//
// main.c
//
// Heretic 2 Save File Converter - 32-bit to 64-bit
//
// Usage: SaveConverter.exe <input.sav> [output.sav]
//        SaveConverter.exe --info <file.sav>
//        SaveConverter.exe --sizes
//

#include "save_convert.h"
#include <string.h>

static void PrintUsage(const char* progName)
{
    printf("\n");
    printf("Heretic 2 Save Converter - 32-bit to 64-bit\n");
    printf("============================================\n\n");
    printf("Usage:\n");
    printf("  %s <input.sav> [output.sav]  - Convert a save file\n", progName);
    printf("  %s --info <file.sav>         - Show save file info\n", progName);
    printf("  %s --sizes                   - Show struct sizes\n", progName);
    printf("  %s --help                    - Show this help\n", progName);
    printf("\n");
    printf("Notes:\n");
    printf("  - This tool converts 32-bit save files to 64-bit format.\n");
    printf("  - If no output file is specified, '_x64' is appended to the name.\n");
    printf("  - Always backup your original save files before converting!\n");
    printf("\n");
    printf("Supported files:\n");
    printf("  .sav  - Game save files (partial support - game_locals only)\n");
    printf("  .sv2  - Level save files (not yet supported)\n");
    printf("\n");
}

static void PrintSaveInfo(const char* path)
{
    printf("\nSave File Information\n");
    printf("=====================\n");
    printf("File: %s\n\n", path);
    
    FILE* f = NULL;
    if (fopen_s(&f, path, "rb") != 0 || f == NULL)
    {
        printf("Error: Cannot open file\n");
        return;
    }
    
    // Get file size
    fseek(f, 0, SEEK_END);
    long fileSize = ftell(f);
    fseek(f, 0, SEEK_SET);
    printf("File size: %ld bytes\n", fileSize);
    
    // Read save version
    char save_ver[16];
    if (fread(save_ver, sizeof(save_ver), 1, f) != 1)
    {
        printf("Error: Cannot read save version\n");
        fclose(f);
        return;
    }
    save_ver[15] = 0;
    printf("Save version: '%s'\n", save_ver);
    
    if (strcmp(save_ver, SAVE_VERSION) != 0)
    {
        printf("Warning: Unknown save version (expected '%s')\n", SAVE_VERSION);
    }
    
    // Read game_locals size
    int game_size;
    if (fread(&game_size, sizeof(game_size), 1, f) != 1)
    {
        printf("Error: Cannot read game_locals size\n");
        fclose(f);
        return;
    }
    printf("game_locals size: %d bytes\n", game_size);
    
    // Determine architecture
    printf("\n");
    if (game_size == X86_GAME_LOCALS_SIZE || game_size < (int)sizeof(game_locals_x64_t))
    {
        printf("Architecture: 32-bit (x86)\n");
        printf("Status: Needs conversion for 64-bit build\n");
        
        // Read game_locals to show info
        if (game_size <= (int)sizeof(game_locals_x86_t))
        {
            game_locals_x86_t game;
            memset(&game, 0, sizeof(game));
            fseek(f, 16 + 4, SEEK_SET); // After version and size
            fread(&game, game_size, 1, f);
            
            printf("\nGame Info:\n");
            printf("  spawnpoint: %s\n", game.spawnpoint);
            printf("  maxclients: %d\n", game.maxclients);
            printf("  maxentities: %d\n", game.maxentities);
            printf("  num_clients: %d\n", game.num_clients);
            printf("  autosaved: %s\n", game.autosaved ? "yes" : "no");
        }
    }
    else if (game_size == (int)sizeof(game_locals_x64_t))
    {
        printf("Architecture: 64-bit (x64)\n");
        printf("Status: Already compatible with 64-bit build\n");
    }
    else
    {
        printf("Architecture: Unknown\n");
        printf("Status: Cannot determine save file format\n");
    }
    
    // Read client size if present
    fseek(f, 16 + 4 + game_size, SEEK_SET);
    int client_size;
    if (fread(&client_size, sizeof(client_size), 1, f) == 1)
    {
        printf("\ngclient size: %d bytes\n", client_size);
    }
    
    fclose(f);
    printf("\n");
}

static char* GenerateOutputPath(const char* inputPath)
{
    static char outputPath[512];
    
    // Find the extension
    const char* ext = strrchr(inputPath, '.');
    if (ext == NULL)
    {
        // No extension, just append _x64
        snprintf(outputPath, sizeof(outputPath), "%s_x64", inputPath);
    }
    else
    {
        // Insert _x64 before extension
        size_t baseLen = ext - inputPath;
        strncpy_s(outputPath, sizeof(outputPath), inputPath, baseLen);
        outputPath[baseLen] = 0;
        strcat_s(outputPath, sizeof(outputPath), "_x64");
        strcat_s(outputPath, sizeof(outputPath), ext);
    }
    
    return outputPath;
}

int main(int argc, char* argv[])
{
    if (argc < 2)
    {
        PrintUsage(argv[0]);
        return 1;
    }
    
    // Check for options
    if (strcmp(argv[1], "--help") == 0 || strcmp(argv[1], "-h") == 0 || strcmp(argv[1], "/?") == 0)
    {
        PrintUsage(argv[0]);
        return 0;
    }
    
    if (strcmp(argv[1], "--sizes") == 0)
    {
        printf("\n");
        PrintStructSizes();
        return 0;
    }
    
    if (strcmp(argv[1], "--info") == 0)
    {
        if (argc < 3)
        {
            printf("Error: --info requires a file path\n");
            return 1;
        }
        PrintSaveInfo(argv[2]);
        return 0;
    }
    
    // Convert mode
    const char* inputPath = argv[1];
    const char* outputPath = (argc >= 3) ? argv[2] : GenerateOutputPath(inputPath);
    
    // Check file extension
    const char* ext = strrchr(inputPath, '.');
    if (ext == NULL)
    {
        printf("Error: Cannot determine file type (no extension)\n");
        return 1;
    }
    
    printf("\n");
    printf("Heretic 2 Save Converter\n");
    printf("========================\n\n");
    
    bool success = false;
    
    if (_stricmp(ext, ".sav") == 0)
    {
        // Check if it's an x86 save
        int gameSize = 0;
        if (!IsX86SaveFile(inputPath, &gameSize, NULL))
        {
            if (gameSize == (int)sizeof(game_locals_x64_t))
            {
                printf("File is already in 64-bit format, no conversion needed.\n");
                return 0;
            }
            else if (gameSize == 0)
            {
                printf("Error: Cannot read save file.\n");
                return 1;
            }
            else
            {
                printf("Warning: Unknown save format (game_locals size = %d)\n", gameSize);
            }
        }
        
        success = ConvertGameSave(inputPath, outputPath);
    }
    else if (_stricmp(ext, ".sv2") == 0)
    {
        success = ConvertLevelSave(inputPath, outputPath);
    }
    else
    {
        printf("Error: Unknown file type '%s'\n", ext);
        printf("Supported types: .sav (game save), .sv2 (level save)\n");
        return 1;
    }
    
    if (success)
    {
        printf("\nConversion successful!\n");
        printf("Output: %s\n\n", outputPath);
        return 0;
    }
    else
    {
        printf("\nConversion failed!\n\n");
        return 1;
    }
}
