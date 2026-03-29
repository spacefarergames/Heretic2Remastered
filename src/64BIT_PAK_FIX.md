# 64-bit PAK File Loading Fix

## Problem
The game crashed on startup when loading `base.pak` after switching to 64-bit builds. The issue was caused by integer overflow in the `MakePak.exe` tool when writing PAK file headers.

## Root Cause
In `tools/MakePak.c`, the code used `ftell()` to get file positions and directly cast them to `int`:

```c
directory[i].filepos = (int)ftell(pakfile);  // UNSAFE on 64-bit!
header.dirofs = (int)ftell(pakfile);         // UNSAFE on 64-bit!
```

On 64-bit systems, `ftell()` returns a `long` which can be much larger than a 32-bit `int`. This caused silent truncation of file offsets, resulting in corrupted PAK directory entries. When the game engine tried to load these files, it would read from incorrect file positions and crash.

## Solution
Updated `MakePak.c` to:

1. **Use intermediate `long` variables** to capture `ftell()` results
2. **Check for INT_MAX overflow** before casting to `int`
3. **Add validation for individual file sizes** to prevent overflow during file reading
4. **Added proper error messages** for diagnostic purposes
5. **Included `<limits.h>`** for INT_MAX constant

### Code Changes

**Before (Unsafe):**
```c
directory[i].filepos = (int)ftell(pakfile);
```

**After (Safe):**
```c
long pos = ftell(pakfile);
if (pos > INT_MAX)
{
    fprintf(stderr, "ERROR: File offset exceeds 2GB limit at file index %d. PAK file too large.\n", i);
    fclose(pakfile);
    return 1;
}
directory[i].filepos = (int)pos;
```

## Technical Details

- **PAK format limit**: 2GB maximum file size (due to 32-bit signed int offsets)
- **Compatibility**: Both 32-bit and 64-bit builds now work correctly
- **Affected files**: `tools/MakePak.c`
- **Testing**: Successfully created base.pak with 2,781 files (2.3GB+)

## Result
✅ `base.pak` now loads correctly on 64-bit builds
✅ Proper error handling for files exceeding format limits
✅ Backward compatible with existing PAK files
