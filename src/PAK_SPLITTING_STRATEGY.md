# PAK File Splitting Strategy for R7

## Analysis of Base Folder Contents

| Directory | Size | Type |
|-----------|------|------|
| HDTextures | 1.688 GB | HD replacement textures (PNG) |
| HDVideos | 0.282 GB | HD video files (MP4/MKV) |
| VIDEO | 0.093 GB | Legacy video files (SMK) |
| music | 0.031 GB | OGG music files |
| players | 0.013 GB | Player models |
| Art | 0.010 GB | Artwork |
| skins | 0.001 GB | Skin files |
| pics | 0.001 GB | Picture files |
| models, sprites, sound, config, ds | <0.001 GB | Game data |
| **Total Loose Files** | **~2.1 GB** | |
| Htic2-0.pak (excluded) | 0.206 GB | Original game pak |
| Htic2-1.pak (excluded) | 0.042 GB | Original game pak |

**Note:** Original PAK files are excluded from packing (MakePak skips .pak files)

## Proposed Splitting Strategy

### PAK File Organization
Split into 3 PAK files targeting ~1.4GB each:

#### 1. base.pak (~1.4 GB) - Core Game Data
- `config/` - Configuration files
- `models/` - Game models
- `sprites/` - Sprite files  
- `sound/` - Sound effects
- `skins/` - Skin files
- `players/` - Player models
- `pics/` - Picture files
- `Art/` - Artwork
- `music/` - OGG music files (0.031 GB)
- `ds/` - Miscellaneous data
- `VIDEO/` - Legacy videos (0.093 GB)
- **Subtotal: ~0.3 GB** (plenty of room for expansion)

#### 2. base_hd.pak (~1.4 GB) - HD Assets
- `HDTextures/` - Split into parts if needed
  - Part 1: ~1.0 GB
  - Part 2: ~0.688 GB remaining
- `HDVideos/` - HD videos (0.282 GB)
- **Subtotal: ~1.37 GB** (nearly at limit)

### Alternative: 4-PAK Strategy (Safer)

#### 1. base.pak (~0.8 GB) - Core Game Data
- Core game content (models, sounds, sprites, etc.)

#### 2. base_textures.pak (~1.2 GB) - HD Textures (Part 1)
- HDTextures/ - First half

#### 3. base_textures2.pak (~0.5 GB) - HD Textures (Part 2) + Video
- HDTextures/ - Second half
- HDVideos/ - All HD videos
- VIDEO/ - Legacy videos

#### 4. base_music.pak (~0.031 GB) - Music
- music/ - OGG files

## Engine Support

The game engine already supports loading multiple PAK files:
```c
// engine loads in order: base.pak, Htic2-0.pak through Htic2-9.pak
// Plus loose files for backward compatibility
```

Proposed naming convention:
- `base.pak` - Primary pak (loaded first)
- `base_hd.pak` - HD assets
- `base_textures2.pak` - Additional assets
- `base_music.pak` - Optional music pak

## Implementation Steps

1. Create staging directories: `base_pak1`, `base_pak2`, `base_pak3`, etc.
2. Copy appropriate files to each staging directory
3. Run MakePak for each staging directory
4. Verify all PAK files create successfully
5. Test game loading with multiple PAK files
6. Update README with loading information

## Benefits

✅ Stays within 2GB format limit  
✅ Parallel loading (OS can read multiple files)  
✅ Modular organization  
✅ Easy to add/remove specific assets  
✅ Backward compatible with engine
