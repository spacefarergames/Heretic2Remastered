//
// cl_mp4.h
//
// MP4 cinematic playback via Windows Media Foundation.
//

#pragma once

#include "../qcommon/qcommon.h"

// Opens an MP4 file and initialises the WMF source reader.
// Calls re.DrawInitCinematic on success.
qboolean    MP4_Open(const char* filepath);

// Releases all WMF resources and calls re.DrawCloseCinematic.
void        MP4_Shutdown(void);

// Returns true while an MP4 file is open and playing.
qboolean    MP4_IsOpen(void);

// Returns true when the last video frame has been decoded.
qboolean    MP4_AtEnd(void);

// Decodes and stores the next video frame; also pushes the matching
// audio chunk to se.RawSamples.
void        MP4_NextFrame(void);

// Returns a pointer to the current BGRA frame (vid_width * vid_height * 4 bytes,
// stored bottom-up as produced by MFVideoFormat_RGB32).
const byte* MP4_GetVideoFrame(void);

int         MP4_GetWidth(void);
int         MP4_GetHeight(void);
float       MP4_GetFPS(void);
int         MP4_GetFrame(void);
