//
// snd_efx.h -- OpenAL EFX (Effects Extension) audio effects system.
//
// Provides EAX-style reverb environments, filters (lowpass, highpass, bandpass),
// occlusion, and additional effects (chorus, distortion, echo, etc.).
//
// Copyright 2025 mxd
//

#pragma once

#include "snd_local.h"

#include <AL/al.h>
#include <AL/alc.h>
#include <AL/efx.h>
#include <AL/efx-presets.h>

//=============================================================================
// EFX Function Pointers
//=============================================================================

// Effect objects
typedef void (AL_APIENTRY* LPALGENEFFECTS)(ALsizei, ALuint*);
typedef void (AL_APIENTRY* LPALDELETEEFFECTS)(ALsizei, const ALuint*);
typedef ALboolean (AL_APIENTRY* LPALISEFFECT)(ALuint);
typedef void (AL_APIENTRY* LPALEFFECTI)(ALuint, ALenum, ALint);
typedef void (AL_APIENTRY* LPALEFFECTIV)(ALuint, ALenum, const ALint*);
typedef void (AL_APIENTRY* LPALEFFECTF)(ALuint, ALenum, ALfloat);
typedef void (AL_APIENTRY* LPALEFFECTFV)(ALuint, ALenum, const ALfloat*);
typedef void (AL_APIENTRY* LPALGETEFFECTI)(ALuint, ALenum, ALint*);
typedef void (AL_APIENTRY* LPALGETEFFECTIV)(ALuint, ALenum, ALint*);
typedef void (AL_APIENTRY* LPALGETEFFECTF)(ALuint, ALenum, ALfloat*);
typedef void (AL_APIENTRY* LPALGETEFFECTFV)(ALuint, ALenum, ALfloat*);

// Filter objects
typedef void (AL_APIENTRY* LPALGENFILTERS)(ALsizei, ALuint*);
typedef void (AL_APIENTRY* LPALDELETEFILTERS)(ALsizei, const ALuint*);
typedef ALboolean (AL_APIENTRY* LPALISFILTER)(ALuint);
typedef void (AL_APIENTRY* LPALFILTERI)(ALuint, ALenum, ALint);
typedef void (AL_APIENTRY* LPALFILTERIV)(ALuint, ALenum, const ALint*);
typedef void (AL_APIENTRY* LPALFILTERF)(ALuint, ALenum, ALfloat);
typedef void (AL_APIENTRY* LPALFILTERFV)(ALuint, ALenum, const ALfloat*);
typedef void (AL_APIENTRY* LPALGETFILTERI)(ALuint, ALenum, ALint*);
typedef void (AL_APIENTRY* LPALGETFILTERIV)(ALuint, ALenum, ALint*);
typedef void (AL_APIENTRY* LPALGETFILTERF)(ALuint, ALenum, ALfloat*);
typedef void (AL_APIENTRY* LPALGETFILTERFV)(ALuint, ALenum, ALfloat*);

// Auxiliary effect slots
typedef void (AL_APIENTRY* LPALGENAUXILIARYEFFECTSLOTS)(ALsizei, ALuint*);
typedef void (AL_APIENTRY* LPALDELETEAUXILIARYEFFECTSLOTS)(ALsizei, const ALuint*);
typedef ALboolean (AL_APIENTRY* LPALISAUXILIARYEFFECTSLOT)(ALuint);
typedef void (AL_APIENTRY* LPALAUXILIARYEFFECTSLOTI)(ALuint, ALenum, ALint);
typedef void (AL_APIENTRY* LPALAUXILIARYEFFECTSLOTIV)(ALuint, ALenum, const ALint*);
typedef void (AL_APIENTRY* LPALAUXILIARYEFFECTSLOTF)(ALuint, ALenum, ALfloat);
typedef void (AL_APIENTRY* LPALAUXILIARYEFFECTSLOTFV)(ALuint, ALenum, const ALfloat*);
typedef void (AL_APIENTRY* LPALGETAUXILIARYEFFECTSLOTI)(ALuint, ALenum, ALint*);
typedef void (AL_APIENTRY* LPALGETAUXILIARYEFFECTSLOTIV)(ALuint, ALenum, ALint*);
typedef void (AL_APIENTRY* LPALGETAUXILIARYEFFECTSLOTF)(ALuint, ALenum, ALfloat*);
typedef void (AL_APIENTRY* LPALGETAUXILIARYEFFECTSLOTFV)(ALuint, ALenum, ALfloat*);

//=============================================================================
// Effect Types Enum
//=============================================================================

typedef enum
{
	EFX_EFFECT_NONE = 0,
	EFX_EFFECT_REVERB,
	EFX_EFFECT_EAXREVERB,
	EFX_EFFECT_CHORUS,
	EFX_EFFECT_DISTORTION,
	EFX_EFFECT_ECHO,
	EFX_EFFECT_FLANGER,
	EFX_EFFECT_FREQUENCY_SHIFTER,
	EFX_EFFECT_VOCAL_MORPHER,
	EFX_EFFECT_PITCH_SHIFTER,
	EFX_EFFECT_RING_MODULATOR,
	EFX_EFFECT_AUTOWAH,
	EFX_EFFECT_COMPRESSOR,
	EFX_EFFECT_EQUALIZER,

	EFX_EFFECT_COUNT
} efx_effect_type_t;

typedef enum
{
	EFX_FILTER_NONE = 0,
	EFX_FILTER_LOWPASS,
	EFX_FILTER_HIGHPASS,
	EFX_FILTER_BANDPASS,

	EFX_FILTER_COUNT
} efx_filter_type_t;

//=============================================================================
// Occlusion Context
//=============================================================================

typedef struct
{
	qboolean enabled;
	float occlusion_factor;     // 0.0 = no occlusion, 1.0 = fully occluded
	float gain_hf;              // High-frequency attenuation (0.0-1.0)
	float gain_lf;              // Low-frequency attenuation (0.0-1.0)
} efx_occlusion_t;

//=============================================================================
// EFX System State
//=============================================================================

typedef struct
{
	qboolean initialized;
	qboolean eax_reverb_supported;
	qboolean chorus_supported;
	qboolean distortion_supported;
	qboolean echo_supported;
	qboolean flanger_supported;
	qboolean frequency_shifter_supported;
	qboolean vocal_morpher_supported;
	qboolean pitch_shifter_supported;
	qboolean ring_modulator_supported;
	qboolean autowah_supported;
	qboolean compressor_supported;
	qboolean equalizer_supported;

	int max_aux_sends;          // Maximum auxiliary sends per source.
	int current_env_index;      // Current EAX environment preset index.

	ALuint reverb_effect;       // Main reverb effect object.
	ALuint reverb_slot;         // Auxiliary effect slot for reverb.

	ALuint underwater_filter;   // Lowpass filter for underwater effect.
	ALuint occlusion_filter;    // Lowpass filter for occlusion.

	efx_occlusion_t occlusion;  // Current occlusion state.
} efx_state_t;

//=============================================================================
// Global EFX Function Pointers (set during initialization)
//=============================================================================

extern LPALGENEFFECTS alGenEffects_ptr;
extern LPALDELETEEFFECTS alDeleteEffects_ptr;
extern LPALISEFFECT alIsEffect_ptr;
extern LPALEFFECTI alEffecti_ptr;
extern LPALEFFECTIV alEffectiv_ptr;
extern LPALEFFECTF alEffectf_ptr;
extern LPALEFFECTFV alEffectfv_ptr;
extern LPALGETEFFECTI alGetEffecti_ptr;
extern LPALGETEFFECTIV alGetEffectiv_ptr;
extern LPALGETEFFECTF alGetEffectf_ptr;
extern LPALGETEFFECTFV alGetEffectfv_ptr;

extern LPALGENFILTERS alGenFilters_ptr;
extern LPALDELETEFILTERS alDeleteFilters_ptr;
extern LPALISFILTER alIsFilter_ptr;
extern LPALFILTERI alFilteri_ptr;
extern LPALFILTERIV alFilteriv_ptr;
extern LPALFILTERF alFilterf_ptr;
extern LPALFILTERFV alFilterfv_ptr;
extern LPALGETFILTERI alGetFilteri_ptr;
extern LPALGETFILTERIV alGetFilteriv_ptr;
extern LPALGETFILTERF alGetFilterf_ptr;
extern LPALGETFILTERFV alGetFilterfv_ptr;

extern LPALGENAUXILIARYEFFECTSLOTS alGenAuxiliaryEffectSlots_ptr;
extern LPALDELETEAUXILIARYEFFECTSLOTS alDeleteAuxiliaryEffectSlots_ptr;
extern LPALISAUXILIARYEFFECTSLOT alIsAuxiliaryEffectSlot_ptr;
extern LPALAUXILIARYEFFECTSLOTI alAuxiliaryEffectSloti_ptr;
extern LPALAUXILIARYEFFECTSLOTIV alAuxiliaryEffectSlotiv_ptr;
extern LPALAUXILIARYEFFECTSLOTF alAuxiliaryEffectSlotf_ptr;
extern LPALAUXILIARYEFFECTSLOTFV alAuxiliaryEffectSlotfv_ptr;
extern LPALGETAUXILIARYEFFECTSLOTI alGetAuxiliaryEffectSloti_ptr;
extern LPALGETAUXILIARYEFFECTSLOTIV alGetAuxiliaryEffectSlotiv_ptr;
extern LPALGETAUXILIARYEFFECTSLOTF alGetAuxiliaryEffectSlotf_ptr;
extern LPALGETAUXILIARYEFFECTSLOTFV alGetAuxiliaryEffectSlotfv_ptr;

//=============================================================================
// EFX System Functions
//=============================================================================

// Initialize the EFX system. Call after OpenAL context is created.
// Returns true if EFX is available and initialized successfully.
extern qboolean EFX_Init(void);

// Shutdown the EFX system. Call before destroying OpenAL context.
extern void EFX_Shutdown(void);

// Returns true if EFX is available and initialized.
extern qboolean EFX_IsActive(void);

// Returns the auxiliary effect slot ID for connecting sources.
extern ALuint EFX_GetReverbSlot(void);

// Returns the current underwater filter ID.
extern ALuint EFX_GetUnderwaterFilter(void);

// Returns the current occlusion filter ID.
extern ALuint EFX_GetOcclusionFilter(void);

//=============================================================================
// EAX Reverb Environment Functions
//=============================================================================

// Set the reverb environment by EAX preset index (see q_Shared.h EAX_ENVIRONMENT_*).
extern void EFX_SetEnvironment(int env_index);

// Get the current environment index.
extern int EFX_GetCurrentEnvironment(void);

// Set a custom reverb preset.
extern void EFX_SetCustomReverb(const EFXEAXREVERBPROPERTIES* props);

//=============================================================================
// Filter Functions
//=============================================================================

// Enable/disable underwater filter effect.
extern void EFX_SetUnderwater(qboolean enabled, float gain_hf);

// Get underwater state.
extern qboolean EFX_IsUnderwater(void);

// Set occlusion parameters for a source.
// occlusion_factor: 0.0 = no occlusion, 1.0 = fully occluded.
extern void EFX_SetOcclusion(float occlusion_factor);

// Apply the current filter state to an OpenAL source.
extern void EFX_ApplySourceFilters(ALuint source, qboolean is_underwater, float occlusion);

// Connect a source to the reverb effect slot.
extern void EFX_ConnectSourceToReverb(ALuint source);

// Disconnect a source from the reverb effect slot.
extern void EFX_DisconnectSourceFromReverb(ALuint source);

//=============================================================================
// Additional Effect Creation (for advanced use)
//=============================================================================

// Create a chorus effect with specified parameters.
// Returns effect ID or 0 on failure.
extern ALuint EFX_CreateChorusEffect(int waveform, int phase, float rate, float depth, float feedback, float delay);

// Create a distortion effect with specified parameters.
extern ALuint EFX_CreateDistortionEffect(float edge, float gain, float lowpass_cutoff, float eq_center, float eq_bandwidth);

// Create an echo effect with specified parameters.
extern ALuint EFX_CreateEchoEffect(float delay, float lr_delay, float damping, float feedback, float spread);

// Create a flanger effect with specified parameters.
extern ALuint EFX_CreateFlangerEffect(int waveform, int phase, float rate, float depth, float feedback, float delay);

// Create a frequency shifter effect.
extern ALuint EFX_CreateFrequencyShifterEffect(float frequency, int left_direction, int right_direction);

// Create a vocal morpher effect.
extern ALuint EFX_CreateVocalMorpherEffect(int phoneme_a, int phoneme_a_tuning, int phoneme_b, int phoneme_b_tuning, int waveform, float rate);

// Create a pitch shifter effect.
extern ALuint EFX_CreatePitchShifterEffect(int coarse_tune, int fine_tune);

// Create a ring modulator effect.
extern ALuint EFX_CreateRingModulatorEffect(float frequency, float highpass_cutoff, int waveform);

// Create an autowah effect.
extern ALuint EFX_CreateAutowahEffect(float attack_time, float release_time, float resonance, float peak_gain);

// Create a compressor effect.
extern ALuint EFX_CreateCompressorEffect(qboolean enabled);

// Create an equalizer effect.
extern ALuint EFX_CreateEqualizerEffect(float low_gain, float low_cutoff, float mid1_gain, float mid1_center, float mid1_width,
	float mid2_gain, float mid2_center, float mid2_width, float high_gain, float high_cutoff);

// Delete an effect created with EFX_Create*Effect functions.
extern void EFX_DeleteEffect(ALuint effect);

//=============================================================================
// Filter Creation
//=============================================================================

// Create a lowpass filter.
extern ALuint EFX_CreateLowpassFilter(float gain, float gain_hf);

// Create a highpass filter.
extern ALuint EFX_CreateHighpassFilter(float gain, float gain_lf);

// Create a bandpass filter.
extern ALuint EFX_CreateBandpassFilter(float gain, float gain_lf, float gain_hf);

// Update filter parameters.
extern void EFX_UpdateLowpassFilter(ALuint filter, float gain, float gain_hf);
extern void EFX_UpdateHighpassFilter(ALuint filter, float gain, float gain_lf);
extern void EFX_UpdateBandpassFilter(ALuint filter, float gain, float gain_lf, float gain_hf);

// Delete a filter.
extern void EFX_DeleteFilter(ALuint filter);
