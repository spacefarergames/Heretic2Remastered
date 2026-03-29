//
// snd_al.h -- OpenAL Soft HRTF loopback rendering layer.
//
// Copyright 2025 mxd
//

#pragma once

#include "snd_local.h"

// Maximum simultaneous AL sources (matches engine MAX_CHANNELS).
#define AL_MAX_SOURCES	MAX_CHANNELS

// Buffer cache entry: maps an sfx_t pointer to an OpenAL buffer ID.
typedef struct
{
	const sfx_t* sfx;
	unsigned int buffer; // AL buffer name.
} al_bufcache_t;

#define AL_BUFCACHE_SIZE (MAX_SOUNDS * 2)

//mxd. Initializes the OpenAL Soft loopback device with HRTF enabled.
// Must be called AFTER SDL3 backend init so that sound.speed/channels are known.
// Returns true on success.
extern qboolean AL_Init(int sample_rate, int channels);

//mxd. Shuts down the OpenAL context and loopback device.
extern void AL_Shutdown(void);

//mxd. Returns true when the HRTF loopback device is active.
extern qboolean AL_IsActive(void);

//mxd. Updates the OpenAL listener position and orientation from the engine vectors.
extern void AL_UpdateListener(const vec3_t origin, const vec3_t forward, const vec3_t right, const vec3_t up);

//mxd. Ensures the given sfx_t has a corresponding AL buffer.
// Called lazily the first time a channel references an sfx after loading.
extern unsigned int AL_GetBuffer(const sfx_t* sfx);

//mxd. Invalidates (deletes) the cached AL buffer for the given sfx.
extern void AL_FreeBuffer(const sfx_t* sfx);

//mxd. Frees all cached AL buffers.
extern void AL_FreeAllBuffers(void);

//mxd. Synchronises engine channels[] to AL sources: starts, stops, and repositions.
// Clears channels whose one-shot sounds have finished in the AL source.
extern void AL_UpdateSources(channel_t* channels, int num_channels);

//mxd. Renders 'num_samples' stereo frames from the loopback device into 'out'.
// The output is 16-bit interleaved stereo (matching the SDL3 paint buffer format).
extern void AL_RenderSamples(short* out, int num_samples);
