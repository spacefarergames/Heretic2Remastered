# PAK File Size Limitation - Solution Guide

## Problem Summary

The PAK2 format has a fundamental limitation: it uses **32-bit signed integers** for file offsets, which limits each PAK file to a maximum size of **2,147,483,647 bytes** (~2.0 GB).

Current situation:
- Base directory size: **4.6 GB**
- PAK file size exceeded limit: **2.3 GB** (174 MB over the limit)
- Excluded files (by format): `.cfg`, `.dll`, `.pak` files are automatically excluded

## Root Cause of Crashes

When the PAK file exceeds INT_MAX (2GB):
1. The directory offset (`dirofs`) field gets corrupted/truncated
2. The PAK loader reads from wrong file positions  
3. Game crashes when trying to access corrupted file data

## Solutions

### Option 1: Use Loose Files (Recommended for Game Distribution)
Keep all files extracted in the `base` directory instead of packing them. The engine supports both loose files and PAK archives:
- **Pros:** Works with current 2GB limitation, no setup needed
- **Cons:** Larger distribution, slower disk access
- **Implementation:** Simply don't create base.pak, let the game use loose files

### Option 2: Create Multiple PAK Files
Split large content across several PAK files:
- `base.pak` — Core game data (textures, models, sounds) - <2GB
- `base_hd.pak` — HD textures and videos - <2GB  
- `base_content.pak` — Additional content - <2GB

**Example:**
```
# Pack only essential game files (exclude videos)
MakePak.exe base base.pak

# Manually remove HD video files and repack if needed
```

### Option 3: Compress or Reduce Assets
Reduce file sizes to fit within the 2GB limit:
- Convert HD videos to smaller resolution/codec
- Compress textures further
- Remove debug files (.pdb, .ilk)

### Option 4: Extend PAK Format to 64-bit (Development Option)
Modify the PAK format to support 64-bit offsets:
- Define new format version (PAK3)
- Update `dpackfile3_t` structure with `int64_t` fields
- Update both MakePak and file loader
- Maintain backward compatibility

## Immediate Fix (What We Did)

Fixed issues in R7:
1. **Fixed endianness bug** — PAK2 header ident was incorrect (0x324B4150 vs 0x32414B50)
2. **Fixed 64-bit casting** — Added proper `long` handling in ftell() operations
3. **Added validation** — MakePak now detects and reports size violations

## Current Status

The PAK2 format now works correctly for files up to 2GB, but distributing 4.6GB of content requires one of the solutions above.

## Recommended Action

**For H2R R7 release:** Use **Option 1 (Loose Files)** - Ship the game with extracted files in the `base` directory. This is the safest and most compatible approach for immediate release.
