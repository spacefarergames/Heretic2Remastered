//
// snd_al.c -- OpenAL Soft HRTF loopback rendering layer.
//
// Uses an OpenAL Soft loopback device to render 3D-spatialized audio with
// HRTF (Head-Related Transfer Function) processing, inspired by Aureal A3D.
// The rendered output is fed back into the existing SDL3 audio stream.
//
// Copyright 2025 mxd
//

#include "snd_al.h"
#include "snd_efx.h"
#include "snd_main.h"
#include "snd_wav.h"

#include <AL/al.h>
#include <AL/alc.h>
#include <AL/alext.h>

// Extension function pointers for loopback rendering.
static LPALCLOOPBACKOPENDEVICESOFT alcLoopbackOpenDeviceSOFT_ptr;
static LPALCISRENDERFORMATSUPPORTEDSOFT alcIsRenderFormatSupportedSOFT_ptr;
static LPALCRENDERSAMPLESSOFT alcRenderSamplesSOFT_ptr;

// Loopback device and context.
static ALCdevice* al_device;
static ALCcontext* al_context;
static qboolean al_active;

// Source pool — one AL source per engine channel.
static ALuint al_sources[AL_MAX_SOURCES];

// Track which sfx is currently bound to each source (for change detection).
static const sfx_t* al_source_sfx[AL_MAX_SOURCES];

// Buffer cache: maps sfx_t* to AL buffer names.
static al_bufcache_t al_bufcache[AL_BUFCACHE_SIZE];
static int al_num_bufcache;

// Distance attenuation constants matching the engine's software mixer.
#define AL_FULLVOLUME_DIST	80.0f // Same as SDL_FULLVOLUME in snd_sdl3.c.

// Track underwater state for filtering.
static qboolean al_underwater = false;

// Track if sources are connected to EFX.
static qboolean al_efx_connected = false;

#pragma region ========================== Buffer cache ==========================

// Finds or creates an AL buffer for the given sfx_t.
unsigned int AL_GetBuffer(const sfx_t* sfx)
{
	if (sfx == NULL || sfx->cache == NULL)
		return 0;

	// Look up in cache.
	for (int i = 0; i < al_num_bufcache; i++)
	{
		if (al_bufcache[i].sfx == sfx)
			return al_bufcache[i].buffer;
	}

	// Not cached — create a new AL buffer from sfxcache_t data.
	const sfxcache_t* sc = sfx->cache;

	ALenum format;
	if (sc->stereo)
		format = (sc->width == 2) ? AL_FORMAT_STEREO16 : AL_FORMAT_STEREO8;
	else
		format = (sc->width == 2) ? AL_FORMAT_MONO16 : AL_FORMAT_MONO8;

	ALuint buf = 0;
	alGenBuffers(1, &buf);

	if (buf == 0)
		return 0;

	const int data_size = sc->length * sc->width * (sc->stereo ? 2 : 1);
	alBufferData(buf, format, sc->data, data_size, sc->speed);

	if (alGetError() != AL_NO_ERROR)
	{
		alDeleteBuffers(1, &buf);
		return 0;
	}

	// Store in cache.
	if (al_num_bufcache < AL_BUFCACHE_SIZE)
	{
		al_bufcache[al_num_bufcache].sfx = sfx;
		al_bufcache[al_num_bufcache].buffer = buf;
		al_num_bufcache++;
	}
	else
	{
		// Cache full — this should not happen during normal gameplay.
		si.Com_DPrintf("AL_GetBuffer: buffer cache full, discarding buffer for '%s'\n", sfx->name);
		alDeleteBuffers(1, &buf);
		return 0;
	}

	return buf;
}

void AL_FreeBuffer(const sfx_t* sfx)
{
	for (int i = 0; i < al_num_bufcache; i++)
	{
		if (al_bufcache[i].sfx == sfx)
		{
			alDeleteBuffers(1, &al_bufcache[i].buffer);
			al_bufcache[i] = al_bufcache[al_num_bufcache - 1];
			al_num_bufcache--;

			return;
		}
	}
}

void AL_FreeAllBuffers(void)
{
	for (int i = 0; i < al_num_bufcache; i++)
		alDeleteBuffers(1, &al_bufcache[i].buffer);

	memset(al_bufcache, 0, sizeof(al_bufcache));
	al_num_bufcache = 0;
}

#pragma endregion

#pragma region ========================== Source management ==========================

void AL_UpdateSources(channel_t* ch, const int num_channels)
{
	for (int i = 0; i < num_channels; i++)
	{
		const ALuint src = al_sources[i];

		if (ch[i].sfx == NULL)
		{
			// Channel is inactive — stop its AL source if playing.
			if (al_source_sfx[i] != NULL)
			{
				alSourceStop(src);
				alSourcei(src, AL_BUFFER, 0); // Detach buffer.
				al_source_sfx[i] = NULL;
			}

			continue;
		}

		// Channel is active.
		const sfx_t* sfx = ch[i].sfx;
		const sfxcache_t* sc = sfx->cache;

		if (sc == NULL)
			continue;

		// Set position. Quake 2 engine coords map directly to OpenAL 3D space.
		const ALfloat pos[3] = { ch[i].origin[0], ch[i].origin[1], ch[i].origin[2] };
		alSourcefv(src, AL_POSITION, pos);

		// Set gain from master volume (0-255 mapped to 0.0-1.0).
		const float gain = (float)ch[i].master_vol / 255.0f;
		alSourcef(src, AL_GAIN, gain);

		// Distance attenuation model.
		// Engine uses linear: gain = 1.0 - max(0, dist - 80) * dist_mult.
		if (ch[i].dist_mult > 0.0f)
		{
			alSourcef(src, AL_REFERENCE_DISTANCE, AL_FULLVOLUME_DIST);
			alSourcef(src, AL_MAX_DISTANCE, AL_FULLVOLUME_DIST + 1.0f / ch[i].dist_mult);
			alSourcef(src, AL_ROLLOFF_FACTOR, 1.0f);
			alSourcei(src, AL_SOURCE_RELATIVE, AL_FALSE);
		}
		else
		{
			// ATTN_NONE: no distance attenuation, always full volume.
			// Play relative to listener at origin so distance = 0.
			const ALfloat zero[3] = { 0.0f, 0.0f, 0.0f };
			alSourcefv(src, AL_POSITION, zero);
			alSourcei(src, AL_SOURCE_RELATIVE, AL_TRUE);
			alSourcef(src, AL_ROLLOFF_FACTOR, 0.0f);
		}

		// Check if we need to (re)start this source.
		if (al_source_sfx[i] != sfx)
		{
			// New sound on this channel — attach buffer and play.
			const ALuint buf = AL_GetBuffer(sfx);

			if (buf == 0)
			{
				al_source_sfx[i] = NULL;
				continue;
			}

			alSourceStop(src);
			alSourcei(src, AL_BUFFER, (ALint)buf);

			// Looping.
			const qboolean should_loop = (ch[i].autosound || (sc->loopstart >= 0));
			alSourcei(src, AL_LOOPING, should_loop ? AL_TRUE : AL_FALSE);

			alSourcePlay(src);
			al_source_sfx[i] = sfx;
		}
		else
		{
			// Same sound — check if it's still playing.
			ALint state = 0;
			alGetSourcei(src, AL_SOURCE_STATE, &state);

			if (state != AL_PLAYING)
			{
				if (ch[i].autosound || (sc->loopstart >= 0))
				{
					// Restart looping sound.
					alSourcePlay(src);
				}
				else
				{
					// One-shot sound finished — clear the engine channel.
					alSourcei(src, AL_BUFFER, 0);
					al_source_sfx[i] = NULL;
					memset(&ch[i], 0, sizeof(ch[i]));
				}
			}
		}
	}
}

#pragma endregion

#pragma region ========================== Listener ==========================

void AL_UpdateListener(const vec3_t origin, const vec3_t forward, const vec3_t right, const vec3_t up)
{
	(void)right; // OpenAL derives 'right' from 'at' and 'up'.

	const ALfloat pos[3] = { origin[0], origin[1], origin[2] };
	alListenerfv(AL_POSITION, pos);

	// OpenAL orientation: 6 floats — forward (at) then up.
	const ALfloat ori[6] = { forward[0], forward[1], forward[2], up[0], up[1], up[2] };
	alListenerfv(AL_ORIENTATION, ori);

	alListenerf(AL_GAIN, s_volume->value);
}

#pragma endregion

#pragma region ========================== Render ==========================

void AL_RenderSamples(short* out, const int num_samples)
{
	if (!al_active || num_samples <= 0)
		return;

	alcRenderSamplesSOFT_ptr(al_device, out, num_samples);
}

#pragma endregion

#pragma region ========================== Init / Shutdown ==========================

qboolean AL_IsActive(void)
{
	return al_active;
}

qboolean AL_Init(const int sample_rate, const int channels)
{
	si.Com_Printf("Initializing OpenAL Soft HRTF loopback device...\n");

	al_active = false;
	al_num_bufcache = 0;
	memset(al_source_sfx, 0, sizeof(al_source_sfx));

	// Load extension function pointers.
	alcLoopbackOpenDeviceSOFT_ptr = (LPALCLOOPBACKOPENDEVICESOFT)alcGetProcAddress(NULL, "alcLoopbackOpenDeviceSOFT");
	alcIsRenderFormatSupportedSOFT_ptr = (LPALCISRENDERFORMATSUPPORTEDSOFT)alcGetProcAddress(NULL, "alcIsRenderFormatSupportedSOFT");
	alcRenderSamplesSOFT_ptr = (LPALCRENDERSAMPLESSOFT)alcGetProcAddress(NULL, "alcRenderSamplesSOFT");

	if (alcLoopbackOpenDeviceSOFT_ptr == NULL || alcIsRenderFormatSupportedSOFT_ptr == NULL || alcRenderSamplesSOFT_ptr == NULL)
	{
		si.Com_Printf("OpenAL Soft loopback extension (ALC_SOFT_loopback) not available.\n");
		return false;
	}

	// Open loopback device.
	al_device = alcLoopbackOpenDeviceSOFT_ptr(NULL);

	if (al_device == NULL)
	{
		si.Com_Printf("Failed to open OpenAL loopback device.\n");
		return false;
	}

	// Verify render format is supported.
	const ALCenum al_channels = (channels >= 2) ? ALC_STEREO_SOFT : ALC_MONO_SOFT;

	if (!alcIsRenderFormatSupportedSOFT_ptr(al_device, sample_rate, al_channels, ALC_SHORT_SOFT))
	{
		si.Com_Printf("OpenAL Soft does not support render format: %d Hz, %s, 16-bit.\n",
			sample_rate, (channels >= 2) ? "stereo" : "mono");
		alcCloseDevice(al_device);
		al_device = NULL;

		return false;
	}

	// Create context with HRTF and loopback format attributes.
	const ALCint attrs[] =
	{
		ALC_HRTF_SOFT, ALC_TRUE,
		ALC_FORMAT_CHANNELS_SOFT, al_channels,
		ALC_FORMAT_TYPE_SOFT, ALC_SHORT_SOFT,
		ALC_FREQUENCY, sample_rate,
		0 // Terminator.
	};

	al_context = alcCreateContext(al_device, attrs);

	if (al_context == NULL)
	{
		si.Com_Printf("Failed to create OpenAL HRTF context.\n");
		alcCloseDevice(al_device);
		al_device = NULL;

		return false;
	}

	alcMakeContextCurrent(al_context);

	// Verify HRTF is actually enabled.
	ALCint hrtf_status = 0;
	alcGetIntegerv(al_device, ALC_HRTF_STATUS_SOFT, 1, &hrtf_status);

	ALCint hrtf_enabled = 0;
	alcGetIntegerv(al_device, ALC_HRTF_SOFT, 1, &hrtf_enabled);

	if (hrtf_enabled)
	{
		const ALchar* hrtf_name = alcGetString(al_device, ALC_HRTF_SPECIFIER_SOFT);
		si.Com_Printf("HRTF enabled: %s\n", (hrtf_name != NULL ? hrtf_name : "unknown profile"));
	}
	else
	{
		si.Com_Printf("Warning: HRTF was requested but is not active (status: 0x%04x).\n", hrtf_status);
	}

	// Use linear distance clamped model to match engine attenuation.
	alDistanceModel(AL_LINEAR_DISTANCE_CLAMPED);

	// Generate source pool.
	alGenSources(AL_MAX_SOURCES, al_sources);

	if (alGetError() != AL_NO_ERROR)
	{
		si.Com_Printf("Failed to allocate %d OpenAL sources.\n", AL_MAX_SOURCES);
		alcMakeContextCurrent(NULL);
		alcDestroyContext(al_context);
		alcCloseDevice(al_device);
		al_context = NULL;
		al_device = NULL;

		return false;
	}

	// Default source properties.
	for (int i = 0; i < AL_MAX_SOURCES; i++)
	{
		alSourcef(al_sources[i], AL_REFERENCE_DISTANCE, AL_FULLVOLUME_DIST);
		alSourcef(al_sources[i], AL_MAX_DISTANCE, 1500.0f);
		alSourcef(al_sources[i], AL_ROLLOFF_FACTOR, 1.0f);
	}

	al_active = true;
	si.Com_Printf("OpenAL Soft HRTF loopback device initialized (%d Hz, %d ch).\n", sample_rate, channels);

	return true;
}

void AL_Shutdown(void)
{
	if (!al_active)
		return;

	si.Com_Printf("Shutting down OpenAL Soft HRTF...\n");

	// Stop and delete all sources.
	alSourceStopv(AL_MAX_SOURCES, al_sources);
	alDeleteSources(AL_MAX_SOURCES, al_sources);
	memset(al_sources, 0, sizeof(al_sources));
	memset(al_source_sfx, 0, sizeof(al_source_sfx));

	// Free buffer cache.
	AL_FreeAllBuffers();

	// Destroy context and device.
	alcMakeContextCurrent(NULL);

	if (al_context != NULL)
	{
		alcDestroyContext(al_context);
		al_context = NULL;
	}

	if (al_device != NULL)
	{
		alcCloseDevice(al_device);
		al_device = NULL;
	}

	al_active = false;
	si.Com_Printf("OpenAL Soft HRTF shut down.\n");
}

#pragma endregion

#pragma region ========================== EFX Integration ==========================

unsigned int AL_GetSourceForChannel(int channel_index)
{
	if (!al_active || channel_index < 0 || channel_index >= AL_MAX_SOURCES)
		return 0;

	return al_sources[channel_index];
}

void AL_ConnectSourcesToEFX(void)
{
	if (!al_active || !EFX_IsActive() || al_efx_connected)
		return;

	for (int i = 0; i < AL_MAX_SOURCES; i++)
		EFX_ConnectSourceToReverb(al_sources[i]);

	al_efx_connected = true;
}

void AL_DisconnectSourcesFromEFX(void)
{
	if (!al_active || !al_efx_connected)
		return;

	for (int i = 0; i < AL_MAX_SOURCES; i++)
		EFX_DisconnectSourceFromReverb(al_sources[i]);

	al_efx_connected = false;
}

void AL_SetUnderwaterState(qboolean underwater)
{
	if (!al_active)
		return;

	al_underwater = underwater;

	if (!EFX_IsActive())
		return;

	// Apply or remove underwater filter to all active sources.
	ALuint filter = underwater ? EFX_GetUnderwaterFilter() : AL_FILTER_NULL;

	for (int i = 0; i < AL_MAX_SOURCES; i++)
		alSourcei(al_sources[i], AL_DIRECT_FILTER, (ALint)filter);
}

#pragma endregion
