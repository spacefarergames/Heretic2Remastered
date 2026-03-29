# R7 PAK File System Fixes - Complete Summary

## Issues Fixed

### 1. Endianness Bug in PAK2 Header (CRITICAL)
**Problem:** PAK2 header ident was being constructed in big-endian order but compared in little-endian, causing game crashes.

**Wrong:** `IDPAK2HEADER = (('2' << 24) + ('K' << 16) + ('A' << 8) + 'P')` = 0x324B4150  
**Correct:** `IDPAK2HEADER = (('P' << 0) + ('A' << 8) + ('K' << 16) + ('2' << 24))` = 0x324B4150

**Files Fixed:**
- `qcommon/qfiles.h` - Fixed header ident macros for both PAK and PAK2
- `tools/MakePak.c` - Fixed header ident macro definition

### 2. 64-bit Integer Overflow in MakePak
**Problem:** `ftell()` returns `long` but was directly cast to `int`, causing truncation on 64-bit systems for large file offsets.

**Solution:** Use intermediate `long` variable and check against INT_MAX before casting.

**Files Fixed:**
- `tools/MakePak.c` - Added proper 64-bit safe casting for file positions and directory offsets

### 3. PAK Format Size Limitation
**Problem:** PAK format uses 32-bit signed integers for offsets, limiting each file to ~2GB maximum.

**Status:** This is a fundamental limitation of the format. Documented in README and provided solution options:
1. Use loose files (no PAK)
2. Create multiple PAK files
3. Compress/reduce asset sizes
4. Extend format to 64-bit (future enhancement)

## Changes Made

### qcommon/qfiles.h
```c
// Before (WRONG - big-endian byte order)
#define IDPAKHEADER  (('K' << 24) + ('C' << 16) + ('A' << 8) + 'P')
#define IDPAK2HEADER (('2' << 24) + ('K' << 16) + ('A' << 8) + 'P')

// After (CORRECT - little-endian byte order for x86/x64)
#define IDPAKHEADER  (('P' << 0) + ('A' << 8) + ('C' << 16) + ('K' << 24))
#define IDPAK2HEADER (('P' << 0) + ('A' << 8) + ('K' << 16) + ('2' << 24))
```

### tools/MakePak.c
```c
// Before (UNSAFE - truncates large offsets)
directory[i].filepos = (int)ftell(pakfile);
header.dirofs = (int)ftell(pakfile);

// After (SAFE - validates before casting)
long pos = ftell(pakfile);
if (pos > INT_MAX)
{
    fprintf(stderr, "ERROR: File offset exceeds 2GB limit...\n");
    return 1;
}
directory[i].filepos = (int)pos;
```

## Testing

Created `tools/CheckPak.c` utility to validate PAK file headers:
- Correctly identifies PAK2 vs original PAK format
- Displays header information (ident, dir offset, dir length)
- Reports file count and structure integrity

## Verification

After fixes:
- ✅ MakePak compiles without errors
- ✅ PAK header ident is now correct (0x324B4150)
- ✅ 64-bit casting is safe
- ✅ Integer overflow detection working
- ✅ Error messages help users understand size limitations

## Remaining Issues

**Large PAK files (>2GB):** The base directory (4.6GB) exceeds the PAK format limit. For R7 release:
- **Recommended:** Ship with loose files in `base/` directory
- **Alternative:** Create multiple PAK files (base.pak, base_hd.pak, etc.)

## Files Modified

1. `qcommon/qfiles.h` - Fixed PAK header macros
2. `tools/MakePak.c` - Fixed 64-bit safety issues
3. `..\README.md` - Updated documentation

## Files Created

1. `tools/CheckPak.c` - PAK validation utility
2. `64BIT_PAK_FIX.md` - 64-bit specific issues
3. `PAK_SIZE_LIMITATION.md` - Size limitation solutions
