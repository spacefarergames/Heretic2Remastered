//
// snd_efx.c -- OpenAL EFX (Effects Extension) audio effects implementation.
//
// Copyright 2025 mxd
//

#include "snd_efx.h"
#include "snd_main.h"

//=============================================================================
// Global EFX Function Pointers
//=============================================================================

LPALGENEFFECTS alGenEffects_ptr;
LPALDELETEEFFECTS alDeleteEffects_ptr;
LPALISEFFECT alIsEffect_ptr;
LPALEFFECTI alEffecti_ptr;
LPALEFFECTIV alEffectiv_ptr;
LPALEFFECTF alEffectf_ptr;
LPALEFFECTFV alEffectfv_ptr;
LPALGETEFFECTI alGetEffecti_ptr;
LPALGETEFFECTIV alGetEffectiv_ptr;
LPALGETEFFECTF alGetEffectf_ptr;
LPALGETEFFECTFV alGetEffectfv_ptr;

LPALGENFILTERS alGenFilters_ptr;
LPALDELETEFILTERS alDeleteFilters_ptr;
LPALISFILTER alIsFilter_ptr;
LPALFILTERI alFilteri_ptr;
LPALFILTERIV alFilteriv_ptr;
LPALFILTERF alFilterf_ptr;
LPALFILTERFV alFilterfv_ptr;
LPALGETFILTERI alGetFilteri_ptr;
LPALGETFILTERIV alGetFilteriv_ptr;
LPALGETFILTERF alGetFilterf_ptr;
LPALGETFILTERFV alGetFilterfv_ptr;

LPALGENAUXILIARYEFFECTSLOTS alGenAuxiliaryEffectSlots_ptr;
LPALDELETEAUXILIARYEFFECTSLOTS alDeleteAuxiliaryEffectSlots_ptr;
LPALISAUXILIARYEFFECTSLOT alIsAuxiliaryEffectSlot_ptr;
LPALAUXILIARYEFFECTSLOTI alAuxiliaryEffectSloti_ptr;
LPALAUXILIARYEFFECTSLOTIV alAuxiliaryEffectSlotiv_ptr;
LPALAUXILIARYEFFECTSLOTF alAuxiliaryEffectSlotf_ptr;
LPALAUXILIARYEFFECTSLOTFV alAuxiliaryEffectSlotfv_ptr;
LPALGETAUXILIARYEFFECTSLOTI alGetAuxiliaryEffectSloti_ptr;
LPALGETAUXILIARYEFFECTSLOTIV alGetAuxiliaryEffectSlotiv_ptr;
LPALGETAUXILIARYEFFECTSLOTF alGetAuxiliaryEffectSlotf_ptr;
LPALGETAUXILIARYEFFECTSLOTFV alGetAuxiliaryEffectSlotfv_ptr;

//=============================================================================
// EFX State
//=============================================================================

static efx_state_t efx_state;

// EAX reverb presets mapped to EAX_ENVIRONMENT_* indices from q_Shared.h.
static const EFXEAXREVERBPROPERTIES efx_reverb_presets[] =
{
	EFX_REVERB_PRESET_GENERIC,         // EAX_ENVIRONMENT_GENERIC
	EFX_REVERB_PRESET_PADDEDCELL,      // EAX_ENVIRONMENT_PADDEDCELL
	EFX_REVERB_PRESET_ROOM,            // EAX_ENVIRONMENT_ROOM
	EFX_REVERB_PRESET_BATHROOM,        // EAX_ENVIRONMENT_BATHROOM
	EFX_REVERB_PRESET_LIVINGROOM,      // EAX_ENVIRONMENT_LIVINGROOM
	EFX_REVERB_PRESET_STONEROOM,       // EAX_ENVIRONMENT_STONEROOM
	EFX_REVERB_PRESET_AUDITORIUM,      // EAX_ENVIRONMENT_AUDITORIUM
	EFX_REVERB_PRESET_CONCERTHALL,     // EAX_ENVIRONMENT_CONCERTHALL
	EFX_REVERB_PRESET_CAVE,            // EAX_ENVIRONMENT_CAVE
	EFX_REVERB_PRESET_ARENA,           // EAX_ENVIRONMENT_ARENA
	EFX_REVERB_PRESET_HANGAR,          // EAX_ENVIRONMENT_HANGAR
	EFX_REVERB_PRESET_CARPETEDHALLWAY, // EAX_ENVIRONMENT_CARPETEDHALLWAY
	EFX_REVERB_PRESET_HALLWAY,         // EAX_ENVIRONMENT_HALLWAY
	EFX_REVERB_PRESET_STONECORRIDOR,   // EAX_ENVIRONMENT_STONECORRIDOR
	EFX_REVERB_PRESET_ALLEY,           // EAX_ENVIRONMENT_ALLEY
	EFX_REVERB_PRESET_FOREST,          // EAX_ENVIRONMENT_FOREST
	EFX_REVERB_PRESET_CITY,            // EAX_ENVIRONMENT_CITY
	EFX_REVERB_PRESET_MOUNTAINS,       // EAX_ENVIRONMENT_MOUNTAINS
	EFX_REVERB_PRESET_QUARRY,          // EAX_ENVIRONMENT_QUARRY
	EFX_REVERB_PRESET_PLAIN,           // EAX_ENVIRONMENT_PLAIN
	EFX_REVERB_PRESET_PARKINGLOT,      // EAX_ENVIRONMENT_PARKINGLOT
	EFX_REVERB_PRESET_SEWERPIPE,       // EAX_ENVIRONMENT_SEWERPIPE
	EFX_REVERB_PRESET_UNDERWATER,      // EAX_ENVIRONMENT_UNDERWATER
	EFX_REVERB_PRESET_DRUGGED,         // EAX_ENVIRONMENT_DRUGGED
	EFX_REVERB_PRESET_DIZZY,           // EAX_ENVIRONMENT_DIZZY
	EFX_REVERB_PRESET_PSYCHOTIC,       // EAX_ENVIRONMENT_PSYCHOTIC
};

#define EFX_NUM_PRESETS (sizeof(efx_reverb_presets) / sizeof(efx_reverb_presets[0]))

// Underwater state.
static qboolean efx_underwater = false;
static float efx_underwater_gain_hf = 0.25f;

//=============================================================================
// Helper Functions
//=============================================================================

static qboolean EFX_LoadFunctionPointers(void)
{
	// Effect functions
	alGenEffects_ptr = (LPALGENEFFECTS)alGetProcAddress("alGenEffects");
	alDeleteEffects_ptr = (LPALDELETEEFFECTS)alGetProcAddress("alDeleteEffects");
	alIsEffect_ptr = (LPALISEFFECT)alGetProcAddress("alIsEffect");
	alEffecti_ptr = (LPALEFFECTI)alGetProcAddress("alEffecti");
	alEffectiv_ptr = (LPALEFFECTIV)alGetProcAddress("alEffectiv");
	alEffectf_ptr = (LPALEFFECTF)alGetProcAddress("alEffectf");
	alEffectfv_ptr = (LPALEFFECTFV)alGetProcAddress("alEffectfv");
	alGetEffecti_ptr = (LPALGETEFFECTI)alGetProcAddress("alGetEffecti");
	alGetEffectiv_ptr = (LPALGETEFFECTIV)alGetProcAddress("alGetEffectiv");
	alGetEffectf_ptr = (LPALGETEFFECTF)alGetProcAddress("alGetEffectf");
	alGetEffectfv_ptr = (LPALGETEFFECTFV)alGetProcAddress("alGetEffectfv");

	// Filter functions
	alGenFilters_ptr = (LPALGENFILTERS)alGetProcAddress("alGenFilters");
	alDeleteFilters_ptr = (LPALDELETEFILTERS)alGetProcAddress("alDeleteFilters");
	alIsFilter_ptr = (LPALISFILTER)alGetProcAddress("alIsFilter");
	alFilteri_ptr = (LPALFILTERI)alGetProcAddress("alFilteri");
	alFilteriv_ptr = (LPALFILTERIV)alGetProcAddress("alFilteriv");
	alFilterf_ptr = (LPALFILTERF)alGetProcAddress("alFilterf");
	alFilterfv_ptr = (LPALFILTERFV)alGetProcAddress("alFilterfv");
	alGetFilteri_ptr = (LPALGETFILTERI)alGetProcAddress("alGetFilteri");
	alGetFilteriv_ptr = (LPALGETFILTERIV)alGetProcAddress("alGetFilteriv");
	alGetFilterf_ptr = (LPALGETFILTERF)alGetProcAddress("alGetFilterf");
	alGetFilterfv_ptr = (LPALGETFILTERFV)alGetProcAddress("alGetFilterfv");

	// Auxiliary effect slot functions
	alGenAuxiliaryEffectSlots_ptr = (LPALGENAUXILIARYEFFECTSLOTS)alGetProcAddress("alGenAuxiliaryEffectSlots");
	alDeleteAuxiliaryEffectSlots_ptr = (LPALDELETEAUXILIARYEFFECTSLOTS)alGetProcAddress("alDeleteAuxiliaryEffectSlots");
	alIsAuxiliaryEffectSlot_ptr = (LPALISAUXILIARYEFFECTSLOT)alGetProcAddress("alIsAuxiliaryEffectSlot");
	alAuxiliaryEffectSloti_ptr = (LPALAUXILIARYEFFECTSLOTI)alGetProcAddress("alAuxiliaryEffectSloti");
	alAuxiliaryEffectSlotiv_ptr = (LPALAUXILIARYEFFECTSLOTIV)alGetProcAddress("alAuxiliaryEffectSlotiv");
	alAuxiliaryEffectSlotf_ptr = (LPALAUXILIARYEFFECTSLOTF)alGetProcAddress("alAuxiliaryEffectSlotf");
	alAuxiliaryEffectSlotfv_ptr = (LPALAUXILIARYEFFECTSLOTFV)alGetProcAddress("alAuxiliaryEffectSlotfv");
	alGetAuxiliaryEffectSloti_ptr = (LPALGETAUXILIARYEFFECTSLOTI)alGetProcAddress("alGetAuxiliaryEffectSloti");
	alGetAuxiliaryEffectSlotiv_ptr = (LPALGETAUXILIARYEFFECTSLOTIV)alGetProcAddress("alGetAuxiliaryEffectSlotiv");
	alGetAuxiliaryEffectSlotf_ptr = (LPALGETAUXILIARYEFFECTSLOTF)alGetProcAddress("alGetAuxiliaryEffectSlotf");
	alGetAuxiliaryEffectSlotfv_ptr = (LPALGETAUXILIARYEFFECTSLOTFV)alGetProcAddress("alGetAuxiliaryEffectSlotfv");

	// Check required functions.
	if (alGenEffects_ptr == NULL || alDeleteEffects_ptr == NULL || alEffecti_ptr == NULL ||
		alEffectf_ptr == NULL || alEffectfv_ptr == NULL ||
		alGenFilters_ptr == NULL || alDeleteFilters_ptr == NULL || alFilteri_ptr == NULL || alFilterf_ptr == NULL ||
		alGenAuxiliaryEffectSlots_ptr == NULL || alDeleteAuxiliaryEffectSlots_ptr == NULL ||
		alAuxiliaryEffectSloti_ptr == NULL || alAuxiliaryEffectSlotf_ptr == NULL)
	{
		return false;
	}

	return true;
}

static void EFX_ApplyEaxReverbProperties(ALuint effect, const EFXEAXREVERBPROPERTIES* props)
{
	if (effect == 0 || props == NULL)
		return;

	alEffectf_ptr(effect, AL_EAXREVERB_DENSITY, props->flDensity);
	alEffectf_ptr(effect, AL_EAXREVERB_DIFFUSION, props->flDiffusion);
	alEffectf_ptr(effect, AL_EAXREVERB_GAIN, props->flGain);
	alEffectf_ptr(effect, AL_EAXREVERB_GAINHF, props->flGainHF);
	alEffectf_ptr(effect, AL_EAXREVERB_GAINLF, props->flGainLF);
	alEffectf_ptr(effect, AL_EAXREVERB_DECAY_TIME, props->flDecayTime);
	alEffectf_ptr(effect, AL_EAXREVERB_DECAY_HFRATIO, props->flDecayHFRatio);
	alEffectf_ptr(effect, AL_EAXREVERB_DECAY_LFRATIO, props->flDecayLFRatio);
	alEffectf_ptr(effect, AL_EAXREVERB_REFLECTIONS_GAIN, props->flReflectionsGain);
	alEffectf_ptr(effect, AL_EAXREVERB_REFLECTIONS_DELAY, props->flReflectionsDelay);
	alEffectfv_ptr(effect, AL_EAXREVERB_REFLECTIONS_PAN, props->flReflectionsPan);
	alEffectf_ptr(effect, AL_EAXREVERB_LATE_REVERB_GAIN, props->flLateReverbGain);
	alEffectf_ptr(effect, AL_EAXREVERB_LATE_REVERB_DELAY, props->flLateReverbDelay);
	alEffectfv_ptr(effect, AL_EAXREVERB_LATE_REVERB_PAN, props->flLateReverbPan);
	alEffectf_ptr(effect, AL_EAXREVERB_ECHO_TIME, props->flEchoTime);
	alEffectf_ptr(effect, AL_EAXREVERB_ECHO_DEPTH, props->flEchoDepth);
	alEffectf_ptr(effect, AL_EAXREVERB_MODULATION_TIME, props->flModulationTime);
	alEffectf_ptr(effect, AL_EAXREVERB_MODULATION_DEPTH, props->flModulationDepth);
	alEffectf_ptr(effect, AL_EAXREVERB_AIR_ABSORPTION_GAINHF, props->flAirAbsorptionGainHF);
	alEffectf_ptr(effect, AL_EAXREVERB_HFREFERENCE, props->flHFReference);
	alEffectf_ptr(effect, AL_EAXREVERB_LFREFERENCE, props->flLFReference);
	alEffectf_ptr(effect, AL_EAXREVERB_ROOM_ROLLOFF_FACTOR, props->flRoomRolloffFactor);
	alEffecti_ptr(effect, AL_EAXREVERB_DECAY_HFLIMIT, props->iDecayHFLimit);
}

static void EFX_ApplyStandardReverbProperties(ALuint effect, const EFXEAXREVERBPROPERTIES* props)
{
	if (effect == 0 || props == NULL)
		return;

	// Standard reverb has fewer parameters than EAX reverb.
	alEffectf_ptr(effect, AL_REVERB_DENSITY, props->flDensity);
	alEffectf_ptr(effect, AL_REVERB_DIFFUSION, props->flDiffusion);
	alEffectf_ptr(effect, AL_REVERB_GAIN, props->flGain);
	alEffectf_ptr(effect, AL_REVERB_GAINHF, props->flGainHF);
	alEffectf_ptr(effect, AL_REVERB_DECAY_TIME, props->flDecayTime);
	alEffectf_ptr(effect, AL_REVERB_DECAY_HFRATIO, props->flDecayHFRatio);
	alEffectf_ptr(effect, AL_REVERB_REFLECTIONS_GAIN, props->flReflectionsGain);
	alEffectf_ptr(effect, AL_REVERB_REFLECTIONS_DELAY, props->flReflectionsDelay);
	alEffectf_ptr(effect, AL_REVERB_LATE_REVERB_GAIN, props->flLateReverbGain);
	alEffectf_ptr(effect, AL_REVERB_LATE_REVERB_DELAY, props->flLateReverbDelay);
	alEffectf_ptr(effect, AL_REVERB_AIR_ABSORPTION_GAINHF, props->flAirAbsorptionGainHF);
	alEffectf_ptr(effect, AL_REVERB_ROOM_ROLLOFF_FACTOR, props->flRoomRolloffFactor);
	alEffecti_ptr(effect, AL_REVERB_DECAY_HFLIMIT, props->iDecayHFLimit);
}

static qboolean EFX_CheckEffectSupport(ALenum effect_type)
{
	ALuint effect = 0;
	alGenEffects_ptr(1, &effect);

	if (alGetError() != AL_NO_ERROR || effect == 0)
		return false;

	alEffecti_ptr(effect, AL_EFFECT_TYPE, effect_type);
	const qboolean supported = (alGetError() == AL_NO_ERROR);

	alDeleteEffects_ptr(1, &effect);

	return supported;
}

//=============================================================================
// EFX System Functions
//=============================================================================

qboolean EFX_Init(void)
{
	si.Com_Printf("Initializing OpenAL EFX effects system...\n");

	memset(&efx_state, 0, sizeof(efx_state));
	efx_state.current_env_index = -1; // Invalid, will be set on first call.

	// Check if EFX extension is available.
	if (!alcIsExtensionPresent(alcGetContextsDevice(alcGetCurrentContext()), "ALC_EXT_EFX"))
	{
		si.Com_Printf("OpenAL EFX extension not available.\n");
		return false;
	}

	// Load function pointers.
	if (!EFX_LoadFunctionPointers())
	{
		si.Com_Printf("Failed to load OpenAL EFX function pointers.\n");
		return false;
	}

	// Query max auxiliary sends.
	ALCdevice* device = alcGetContextsDevice(alcGetCurrentContext());
	alcGetIntegerv(device, ALC_MAX_AUXILIARY_SENDS, 1, &efx_state.max_aux_sends);
	si.Com_Printf("EFX max auxiliary sends: %d\n", efx_state.max_aux_sends);

	if (efx_state.max_aux_sends < 1)
	{
		si.Com_Printf("Warning: No auxiliary sends available for effects.\n");
	}

	// Check support for various effect types.
	efx_state.eax_reverb_supported = EFX_CheckEffectSupport(AL_EFFECT_EAXREVERB);
	efx_state.chorus_supported = EFX_CheckEffectSupport(AL_EFFECT_CHORUS);
	efx_state.distortion_supported = EFX_CheckEffectSupport(AL_EFFECT_DISTORTION);
	efx_state.echo_supported = EFX_CheckEffectSupport(AL_EFFECT_ECHO);
	efx_state.flanger_supported = EFX_CheckEffectSupport(AL_EFFECT_FLANGER);
	efx_state.frequency_shifter_supported = EFX_CheckEffectSupport(AL_EFFECT_FREQUENCY_SHIFTER);
	efx_state.vocal_morpher_supported = EFX_CheckEffectSupport(AL_EFFECT_VOCAL_MORPHER);
	efx_state.pitch_shifter_supported = EFX_CheckEffectSupport(AL_EFFECT_PITCH_SHIFTER);
	efx_state.ring_modulator_supported = EFX_CheckEffectSupport(AL_EFFECT_RING_MODULATOR);
	efx_state.autowah_supported = EFX_CheckEffectSupport(AL_EFFECT_AUTOWAH);
	efx_state.compressor_supported = EFX_CheckEffectSupport(AL_EFFECT_COMPRESSOR);
	efx_state.equalizer_supported = EFX_CheckEffectSupport(AL_EFFECT_EQUALIZER);

	si.Com_Printf("EFX effect support:\n");
	si.Com_Printf("  EAX Reverb: %s\n", efx_state.eax_reverb_supported ? "yes" : "no");
	si.Com_Printf("  Chorus: %s\n", efx_state.chorus_supported ? "yes" : "no");
	si.Com_Printf("  Distortion: %s\n", efx_state.distortion_supported ? "yes" : "no");
	si.Com_Printf("  Echo: %s\n", efx_state.echo_supported ? "yes" : "no");
	si.Com_Printf("  Flanger: %s\n", efx_state.flanger_supported ? "yes" : "no");
	si.Com_Printf("  Frequency Shifter: %s\n", efx_state.frequency_shifter_supported ? "yes" : "no");
	si.Com_Printf("  Vocal Morpher: %s\n", efx_state.vocal_morpher_supported ? "yes" : "no");
	si.Com_Printf("  Pitch Shifter: %s\n", efx_state.pitch_shifter_supported ? "yes" : "no");
	si.Com_Printf("  Ring Modulator: %s\n", efx_state.ring_modulator_supported ? "yes" : "no");
	si.Com_Printf("  Autowah: %s\n", efx_state.autowah_supported ? "yes" : "no");
	si.Com_Printf("  Compressor: %s\n", efx_state.compressor_supported ? "yes" : "no");
	si.Com_Printf("  Equalizer: %s\n", efx_state.equalizer_supported ? "yes" : "no");

	// Create the reverb effect.
	alGenEffects_ptr(1, &efx_state.reverb_effect);

	if (alGetError() != AL_NO_ERROR || efx_state.reverb_effect == 0)
	{
		si.Com_Printf("Failed to create reverb effect.\n");
		return false;
	}

	// Set effect type to EAX reverb if supported, otherwise standard reverb.
	if (efx_state.eax_reverb_supported)
	{
		alEffecti_ptr(efx_state.reverb_effect, AL_EFFECT_TYPE, AL_EFFECT_EAXREVERB);
		si.Com_Printf("Using EAX Reverb effect.\n");
	}
	else
	{
		alEffecti_ptr(efx_state.reverb_effect, AL_EFFECT_TYPE, AL_EFFECT_REVERB);
		si.Com_Printf("Using standard Reverb effect (EAX Reverb not supported).\n");
	}

	// Create the auxiliary effect slot.
	alGenAuxiliaryEffectSlots_ptr(1, &efx_state.reverb_slot);

	if (alGetError() != AL_NO_ERROR || efx_state.reverb_slot == 0)
	{
		si.Com_Printf("Failed to create auxiliary effect slot.\n");
		alDeleteEffects_ptr(1, &efx_state.reverb_effect);
		efx_state.reverb_effect = 0;
		return false;
	}

	// Attach the reverb effect to the slot.
	alAuxiliaryEffectSloti_ptr(efx_state.reverb_slot, AL_EFFECTSLOT_EFFECT, (ALint)efx_state.reverb_effect);
	alAuxiliaryEffectSlotf_ptr(efx_state.reverb_slot, AL_EFFECTSLOT_GAIN, 1.0f);

	// Create underwater lowpass filter.
	alGenFilters_ptr(1, &efx_state.underwater_filter);

	if (alGetError() == AL_NO_ERROR && efx_state.underwater_filter != 0)
	{
		alFilteri_ptr(efx_state.underwater_filter, AL_FILTER_TYPE, AL_FILTER_LOWPASS);
		alFilterf_ptr(efx_state.underwater_filter, AL_LOWPASS_GAIN, 1.0f);
		alFilterf_ptr(efx_state.underwater_filter, AL_LOWPASS_GAINHF, 0.25f); // Default underwater HF gain.
	}

	// Create occlusion lowpass filter.
	alGenFilters_ptr(1, &efx_state.occlusion_filter);

	if (alGetError() == AL_NO_ERROR && efx_state.occlusion_filter != 0)
	{
		alFilteri_ptr(efx_state.occlusion_filter, AL_FILTER_TYPE, AL_FILTER_LOWPASS);
		alFilterf_ptr(efx_state.occlusion_filter, AL_LOWPASS_GAIN, 1.0f);
		alFilterf_ptr(efx_state.occlusion_filter, AL_LOWPASS_GAINHF, 1.0f); // No occlusion by default.
	}

	// Set default environment.
	EFX_SetEnvironment(0); // EAX_ENVIRONMENT_GENERIC

	efx_state.initialized = true;
	si.Com_Printf("OpenAL EFX initialized successfully.\n");

	return true;
}

void EFX_Shutdown(void)
{
	if (!efx_state.initialized)
		return;

	si.Com_Printf("Shutting down OpenAL EFX...\n");

	// Delete filters.
	if (efx_state.underwater_filter != 0)
	{
		alDeleteFilters_ptr(1, &efx_state.underwater_filter);
		efx_state.underwater_filter = 0;
	}

	if (efx_state.occlusion_filter != 0)
	{
		alDeleteFilters_ptr(1, &efx_state.occlusion_filter);
		efx_state.occlusion_filter = 0;
	}

	// Delete effect slot.
	if (efx_state.reverb_slot != 0)
	{
		alAuxiliaryEffectSloti_ptr(efx_state.reverb_slot, AL_EFFECTSLOT_EFFECT, AL_EFFECT_NULL);
		alDeleteAuxiliaryEffectSlots_ptr(1, &efx_state.reverb_slot);
		efx_state.reverb_slot = 0;
	}

	// Delete effect.
	if (efx_state.reverb_effect != 0)
	{
		alDeleteEffects_ptr(1, &efx_state.reverb_effect);
		efx_state.reverb_effect = 0;
	}

	efx_state.initialized = false;
	si.Com_Printf("OpenAL EFX shut down.\n");
}

qboolean EFX_IsActive(void)
{
	return efx_state.initialized;
}

ALuint EFX_GetReverbSlot(void)
{
	return efx_state.reverb_slot;
}

ALuint EFX_GetUnderwaterFilter(void)
{
	return efx_state.underwater_filter;
}

ALuint EFX_GetOcclusionFilter(void)
{
	return efx_state.occlusion_filter;
}

//=============================================================================
// EAX Reverb Environment Functions
//=============================================================================

void EFX_SetEnvironment(int env_index)
{
	if (!efx_state.initialized || efx_state.reverb_effect == 0)
		return;

	// Clamp index.
	if (env_index < 0 || env_index >= (int)EFX_NUM_PRESETS)
		env_index = 0;

	if (env_index == efx_state.current_env_index)
		return; // No change.

	const EFXEAXREVERBPROPERTIES* props = &efx_reverb_presets[env_index];

	if (efx_state.eax_reverb_supported)
		EFX_ApplyEaxReverbProperties(efx_state.reverb_effect, props);
	else
		EFX_ApplyStandardReverbProperties(efx_state.reverb_effect, props);

	// Re-attach the effect to the slot to update it.
	alAuxiliaryEffectSloti_ptr(efx_state.reverb_slot, AL_EFFECTSLOT_EFFECT, (ALint)efx_state.reverb_effect);

	efx_state.current_env_index = env_index;
}

int EFX_GetCurrentEnvironment(void)
{
	return efx_state.current_env_index;
}

void EFX_SetCustomReverb(const EFXEAXREVERBPROPERTIES* props)
{
	if (!efx_state.initialized || efx_state.reverb_effect == 0 || props == NULL)
		return;

	if (efx_state.eax_reverb_supported)
		EFX_ApplyEaxReverbProperties(efx_state.reverb_effect, props);
	else
		EFX_ApplyStandardReverbProperties(efx_state.reverb_effect, props);

	alAuxiliaryEffectSloti_ptr(efx_state.reverb_slot, AL_EFFECTSLOT_EFFECT, (ALint)efx_state.reverb_effect);

	efx_state.current_env_index = -1; // Custom, not a preset.
}

//=============================================================================
// Filter Functions
//=============================================================================

void EFX_SetUnderwater(qboolean enabled, float gain_hf)
{
	efx_underwater = enabled;
	efx_underwater_gain_hf = Clamp(gain_hf, 0.0f, 1.0f);

	if (efx_state.initialized && efx_state.underwater_filter != 0)
		alFilterf_ptr(efx_state.underwater_filter, AL_LOWPASS_GAINHF, efx_underwater_gain_hf);
}

qboolean EFX_IsUnderwater(void)
{
	return efx_underwater;
}

void EFX_SetOcclusion(float occlusion_factor)
{
	if (!efx_state.initialized || efx_state.occlusion_filter == 0)
		return;

	efx_state.occlusion.occlusion_factor = Clamp(occlusion_factor, 0.0f, 1.0f);

	// Map occlusion factor to HF gain: 0 occlusion = 1.0 gain, 1 occlusion = 0.1 gain.
	const float gain_hf = 1.0f - (occlusion_factor * 0.9f);
	efx_state.occlusion.gain_hf = gain_hf;

	alFilterf_ptr(efx_state.occlusion_filter, AL_LOWPASS_GAINHF, gain_hf);
}

void EFX_ApplySourceFilters(ALuint source, qboolean is_underwater, float occlusion)
{
	if (!efx_state.initialized || source == 0)
		return;

	ALuint filter = AL_FILTER_NULL;

	if (is_underwater && efx_state.underwater_filter != 0)
	{
		filter = efx_state.underwater_filter;
	}
	else if (occlusion > 0.0f && efx_state.occlusion_filter != 0)
	{
		// Update occlusion filter with current value.
		const float gain_hf = 1.0f - (occlusion * 0.9f);
		alFilterf_ptr(efx_state.occlusion_filter, AL_LOWPASS_GAINHF, gain_hf);
		filter = efx_state.occlusion_filter;
	}

	alSourcei(source, AL_DIRECT_FILTER, (ALint)filter);
}

void EFX_ConnectSourceToReverb(ALuint source)
{
	if (!efx_state.initialized || efx_state.reverb_slot == 0 || source == 0)
		return;

	// Connect source to the reverb effect slot at send 0, with no filter on the wet path.
	alSource3i(source, AL_AUXILIARY_SEND_FILTER, (ALint)efx_state.reverb_slot, 0, AL_FILTER_NULL);

	// Enable auto-send gain adjustment.
	alSourcei(source, AL_AUXILIARY_SEND_FILTER_GAIN_AUTO, AL_TRUE);
	alSourcei(source, AL_AUXILIARY_SEND_FILTER_GAINHF_AUTO, AL_TRUE);
}

void EFX_DisconnectSourceFromReverb(ALuint source)
{
	if (!efx_state.initialized || source == 0)
		return;

	// Disconnect source from effect slot.
	alSource3i(source, AL_AUXILIARY_SEND_FILTER, AL_EFFECTSLOT_NULL, 0, AL_FILTER_NULL);
}

//=============================================================================
// Additional Effect Creation
//=============================================================================

ALuint EFX_CreateChorusEffect(int waveform, int phase, float rate, float depth, float feedback, float delay)
{
	if (!efx_state.initialized || !efx_state.chorus_supported)
		return 0;

	ALuint effect = 0;
	alGenEffects_ptr(1, &effect);

	if (alGetError() != AL_NO_ERROR || effect == 0)
		return 0;

	alEffecti_ptr(effect, AL_EFFECT_TYPE, AL_EFFECT_CHORUS);
	alEffecti_ptr(effect, AL_CHORUS_WAVEFORM, waveform);
	alEffecti_ptr(effect, AL_CHORUS_PHASE, phase);
	alEffectf_ptr(effect, AL_CHORUS_RATE, rate);
	alEffectf_ptr(effect, AL_CHORUS_DEPTH, depth);
	alEffectf_ptr(effect, AL_CHORUS_FEEDBACK, feedback);
	alEffectf_ptr(effect, AL_CHORUS_DELAY, delay);

	if (alGetError() != AL_NO_ERROR)
	{
		alDeleteEffects_ptr(1, &effect);
		return 0;
	}

	return effect;
}

ALuint EFX_CreateDistortionEffect(float edge, float gain, float lowpass_cutoff, float eq_center, float eq_bandwidth)
{
	if (!efx_state.initialized || !efx_state.distortion_supported)
		return 0;

	ALuint effect = 0;
	alGenEffects_ptr(1, &effect);

	if (alGetError() != AL_NO_ERROR || effect == 0)
		return 0;

	alEffecti_ptr(effect, AL_EFFECT_TYPE, AL_EFFECT_DISTORTION);
	alEffectf_ptr(effect, AL_DISTORTION_EDGE, edge);
	alEffectf_ptr(effect, AL_DISTORTION_GAIN, gain);
	alEffectf_ptr(effect, AL_DISTORTION_LOWPASS_CUTOFF, lowpass_cutoff);
	alEffectf_ptr(effect, AL_DISTORTION_EQCENTER, eq_center);
	alEffectf_ptr(effect, AL_DISTORTION_EQBANDWIDTH, eq_bandwidth);

	if (alGetError() != AL_NO_ERROR)
	{
		alDeleteEffects_ptr(1, &effect);
		return 0;
	}

	return effect;
}

ALuint EFX_CreateEchoEffect(float delay, float lr_delay, float damping, float feedback, float spread)
{
	if (!efx_state.initialized || !efx_state.echo_supported)
		return 0;

	ALuint effect = 0;
	alGenEffects_ptr(1, &effect);

	if (alGetError() != AL_NO_ERROR || effect == 0)
		return 0;

	alEffecti_ptr(effect, AL_EFFECT_TYPE, AL_EFFECT_ECHO);
	alEffectf_ptr(effect, AL_ECHO_DELAY, delay);
	alEffectf_ptr(effect, AL_ECHO_LRDELAY, lr_delay);
	alEffectf_ptr(effect, AL_ECHO_DAMPING, damping);
	alEffectf_ptr(effect, AL_ECHO_FEEDBACK, feedback);
	alEffectf_ptr(effect, AL_ECHO_SPREAD, spread);

	if (alGetError() != AL_NO_ERROR)
	{
		alDeleteEffects_ptr(1, &effect);
		return 0;
	}

	return effect;
}

ALuint EFX_CreateFlangerEffect(int waveform, int phase, float rate, float depth, float feedback, float delay)
{
	if (!efx_state.initialized || !efx_state.flanger_supported)
		return 0;

	ALuint effect = 0;
	alGenEffects_ptr(1, &effect);

	if (alGetError() != AL_NO_ERROR || effect == 0)
		return 0;

	alEffecti_ptr(effect, AL_EFFECT_TYPE, AL_EFFECT_FLANGER);
	alEffecti_ptr(effect, AL_FLANGER_WAVEFORM, waveform);
	alEffecti_ptr(effect, AL_FLANGER_PHASE, phase);
	alEffectf_ptr(effect, AL_FLANGER_RATE, rate);
	alEffectf_ptr(effect, AL_FLANGER_DEPTH, depth);
	alEffectf_ptr(effect, AL_FLANGER_FEEDBACK, feedback);
	alEffectf_ptr(effect, AL_FLANGER_DELAY, delay);

	if (alGetError() != AL_NO_ERROR)
	{
		alDeleteEffects_ptr(1, &effect);
		return 0;
	}

	return effect;
}

ALuint EFX_CreateFrequencyShifterEffect(float frequency, int left_direction, int right_direction)
{
	if (!efx_state.initialized || !efx_state.frequency_shifter_supported)
		return 0;

	ALuint effect = 0;
	alGenEffects_ptr(1, &effect);

	if (alGetError() != AL_NO_ERROR || effect == 0)
		return 0;

	alEffecti_ptr(effect, AL_EFFECT_TYPE, AL_EFFECT_FREQUENCY_SHIFTER);
	alEffectf_ptr(effect, AL_FREQUENCY_SHIFTER_FREQUENCY, frequency);
	alEffecti_ptr(effect, AL_FREQUENCY_SHIFTER_LEFT_DIRECTION, left_direction);
	alEffecti_ptr(effect, AL_FREQUENCY_SHIFTER_RIGHT_DIRECTION, right_direction);

	if (alGetError() != AL_NO_ERROR)
	{
		alDeleteEffects_ptr(1, &effect);
		return 0;
	}

	return effect;
}

ALuint EFX_CreateVocalMorpherEffect(int phoneme_a, int phoneme_a_tuning, int phoneme_b, int phoneme_b_tuning, int waveform, float rate)
{
	if (!efx_state.initialized || !efx_state.vocal_morpher_supported)
		return 0;

	ALuint effect = 0;
	alGenEffects_ptr(1, &effect);

	if (alGetError() != AL_NO_ERROR || effect == 0)
		return 0;

	alEffecti_ptr(effect, AL_EFFECT_TYPE, AL_EFFECT_VOCAL_MORPHER);
	alEffecti_ptr(effect, AL_VOCAL_MORPHER_PHONEMEA, phoneme_a);
	alEffecti_ptr(effect, AL_VOCAL_MORPHER_PHONEMEA_COARSE_TUNING, phoneme_a_tuning);
	alEffecti_ptr(effect, AL_VOCAL_MORPHER_PHONEMEB, phoneme_b);
	alEffecti_ptr(effect, AL_VOCAL_MORPHER_PHONEMEB_COARSE_TUNING, phoneme_b_tuning);
	alEffecti_ptr(effect, AL_VOCAL_MORPHER_WAVEFORM, waveform);
	alEffectf_ptr(effect, AL_VOCAL_MORPHER_RATE, rate);

	if (alGetError() != AL_NO_ERROR)
	{
		alDeleteEffects_ptr(1, &effect);
		return 0;
	}

	return effect;
}

ALuint EFX_CreatePitchShifterEffect(int coarse_tune, int fine_tune)
{
	if (!efx_state.initialized || !efx_state.pitch_shifter_supported)
		return 0;

	ALuint effect = 0;
	alGenEffects_ptr(1, &effect);

	if (alGetError() != AL_NO_ERROR || effect == 0)
		return 0;

	alEffecti_ptr(effect, AL_EFFECT_TYPE, AL_EFFECT_PITCH_SHIFTER);
	alEffecti_ptr(effect, AL_PITCH_SHIFTER_COARSE_TUNE, coarse_tune);
	alEffecti_ptr(effect, AL_PITCH_SHIFTER_FINE_TUNE, fine_tune);

	if (alGetError() != AL_NO_ERROR)
	{
		alDeleteEffects_ptr(1, &effect);
		return 0;
	}

	return effect;
}

ALuint EFX_CreateRingModulatorEffect(float frequency, float highpass_cutoff, int waveform)
{
	if (!efx_state.initialized || !efx_state.ring_modulator_supported)
		return 0;

	ALuint effect = 0;
	alGenEffects_ptr(1, &effect);

	if (alGetError() != AL_NO_ERROR || effect == 0)
		return 0;

	alEffecti_ptr(effect, AL_EFFECT_TYPE, AL_EFFECT_RING_MODULATOR);
	alEffectf_ptr(effect, AL_RING_MODULATOR_FREQUENCY, frequency);
	alEffectf_ptr(effect, AL_RING_MODULATOR_HIGHPASS_CUTOFF, highpass_cutoff);
	alEffecti_ptr(effect, AL_RING_MODULATOR_WAVEFORM, waveform);

	if (alGetError() != AL_NO_ERROR)
	{
		alDeleteEffects_ptr(1, &effect);
		return 0;
	}

	return effect;
}

ALuint EFX_CreateAutowahEffect(float attack_time, float release_time, float resonance, float peak_gain)
{
	if (!efx_state.initialized || !efx_state.autowah_supported)
		return 0;

	ALuint effect = 0;
	alGenEffects_ptr(1, &effect);

	if (alGetError() != AL_NO_ERROR || effect == 0)
		return 0;

	alEffecti_ptr(effect, AL_EFFECT_TYPE, AL_EFFECT_AUTOWAH);
	alEffectf_ptr(effect, AL_AUTOWAH_ATTACK_TIME, attack_time);
	alEffectf_ptr(effect, AL_AUTOWAH_RELEASE_TIME, release_time);
	alEffectf_ptr(effect, AL_AUTOWAH_RESONANCE, resonance);
	alEffectf_ptr(effect, AL_AUTOWAH_PEAK_GAIN, peak_gain);

	if (alGetError() != AL_NO_ERROR)
	{
		alDeleteEffects_ptr(1, &effect);
		return 0;
	}

	return effect;
}

ALuint EFX_CreateCompressorEffect(qboolean enabled)
{
	if (!efx_state.initialized || !efx_state.compressor_supported)
		return 0;

	ALuint effect = 0;
	alGenEffects_ptr(1, &effect);

	if (alGetError() != AL_NO_ERROR || effect == 0)
		return 0;

	alEffecti_ptr(effect, AL_EFFECT_TYPE, AL_EFFECT_COMPRESSOR);
	alEffecti_ptr(effect, AL_COMPRESSOR_ONOFF, enabled ? AL_TRUE : AL_FALSE);

	if (alGetError() != AL_NO_ERROR)
	{
		alDeleteEffects_ptr(1, &effect);
		return 0;
	}

	return effect;
}

ALuint EFX_CreateEqualizerEffect(float low_gain, float low_cutoff, float mid1_gain, float mid1_center, float mid1_width,
	float mid2_gain, float mid2_center, float mid2_width, float high_gain, float high_cutoff)
{
	if (!efx_state.initialized || !efx_state.equalizer_supported)
		return 0;

	ALuint effect = 0;
	alGenEffects_ptr(1, &effect);

	if (alGetError() != AL_NO_ERROR || effect == 0)
		return 0;

	alEffecti_ptr(effect, AL_EFFECT_TYPE, AL_EFFECT_EQUALIZER);
	alEffectf_ptr(effect, AL_EQUALIZER_LOW_GAIN, low_gain);
	alEffectf_ptr(effect, AL_EQUALIZER_LOW_CUTOFF, low_cutoff);
	alEffectf_ptr(effect, AL_EQUALIZER_MID1_GAIN, mid1_gain);
	alEffectf_ptr(effect, AL_EQUALIZER_MID1_CENTER, mid1_center);
	alEffectf_ptr(effect, AL_EQUALIZER_MID1_WIDTH, mid1_width);
	alEffectf_ptr(effect, AL_EQUALIZER_MID2_GAIN, mid2_gain);
	alEffectf_ptr(effect, AL_EQUALIZER_MID2_CENTER, mid2_center);
	alEffectf_ptr(effect, AL_EQUALIZER_MID2_WIDTH, mid2_width);
	alEffectf_ptr(effect, AL_EQUALIZER_HIGH_GAIN, high_gain);
	alEffectf_ptr(effect, AL_EQUALIZER_HIGH_CUTOFF, high_cutoff);

	if (alGetError() != AL_NO_ERROR)
	{
		alDeleteEffects_ptr(1, &effect);
		return 0;
	}

	return effect;
}

void EFX_DeleteEffect(ALuint effect)
{
	if (!efx_state.initialized || effect == 0)
		return;

	alDeleteEffects_ptr(1, &effect);
}

//=============================================================================
// Filter Creation
//=============================================================================

ALuint EFX_CreateLowpassFilter(float gain, float gain_hf)
{
	if (!efx_state.initialized)
		return 0;

	ALuint filter = 0;
	alGenFilters_ptr(1, &filter);

	if (alGetError() != AL_NO_ERROR || filter == 0)
		return 0;

	alFilteri_ptr(filter, AL_FILTER_TYPE, AL_FILTER_LOWPASS);
	alFilterf_ptr(filter, AL_LOWPASS_GAIN, Clamp(gain, 0.0f, 1.0f));
	alFilterf_ptr(filter, AL_LOWPASS_GAINHF, Clamp(gain_hf, 0.0f, 1.0f));

	if (alGetError() != AL_NO_ERROR)
	{
		alDeleteFilters_ptr(1, &filter);
		return 0;
	}

	return filter;
}

ALuint EFX_CreateHighpassFilter(float gain, float gain_lf)
{
	if (!efx_state.initialized)
		return 0;

	ALuint filter = 0;
	alGenFilters_ptr(1, &filter);

	if (alGetError() != AL_NO_ERROR || filter == 0)
		return 0;

	alFilteri_ptr(filter, AL_FILTER_TYPE, AL_FILTER_HIGHPASS);
	alFilterf_ptr(filter, AL_HIGHPASS_GAIN, Clamp(gain, 0.0f, 1.0f));
	alFilterf_ptr(filter, AL_HIGHPASS_GAINLF, Clamp(gain_lf, 0.0f, 1.0f));

	if (alGetError() != AL_NO_ERROR)
	{
		alDeleteFilters_ptr(1, &filter);
		return 0;
	}

	return filter;
}

ALuint EFX_CreateBandpassFilter(float gain, float gain_lf, float gain_hf)
{
	if (!efx_state.initialized)
		return 0;

	ALuint filter = 0;
	alGenFilters_ptr(1, &filter);

	if (alGetError() != AL_NO_ERROR || filter == 0)
		return 0;

	alFilteri_ptr(filter, AL_FILTER_TYPE, AL_FILTER_BANDPASS);
	alFilterf_ptr(filter, AL_BANDPASS_GAIN, Clamp(gain, 0.0f, 1.0f));
	alFilterf_ptr(filter, AL_BANDPASS_GAINLF, Clamp(gain_lf, 0.0f, 1.0f));
	alFilterf_ptr(filter, AL_BANDPASS_GAINHF, Clamp(gain_hf, 0.0f, 1.0f));

	if (alGetError() != AL_NO_ERROR)
	{
		alDeleteFilters_ptr(1, &filter);
		return 0;
	}

	return filter;
}

void EFX_UpdateLowpassFilter(ALuint filter, float gain, float gain_hf)
{
	if (!efx_state.initialized || filter == 0)
		return;

	alFilterf_ptr(filter, AL_LOWPASS_GAIN, Clamp(gain, 0.0f, 1.0f));
	alFilterf_ptr(filter, AL_LOWPASS_GAINHF, Clamp(gain_hf, 0.0f, 1.0f));
}

void EFX_UpdateHighpassFilter(ALuint filter, float gain, float gain_lf)
{
	if (!efx_state.initialized || filter == 0)
		return;

	alFilterf_ptr(filter, AL_HIGHPASS_GAIN, Clamp(gain, 0.0f, 1.0f));
	alFilterf_ptr(filter, AL_HIGHPASS_GAINLF, Clamp(gain_lf, 0.0f, 1.0f));
}

void EFX_UpdateBandpassFilter(ALuint filter, float gain, float gain_lf, float gain_hf)
{
	if (!efx_state.initialized || filter == 0)
		return;

	alFilterf_ptr(filter, AL_BANDPASS_GAIN, Clamp(gain, 0.0f, 1.0f));
	alFilterf_ptr(filter, AL_BANDPASS_GAINLF, Clamp(gain_lf, 0.0f, 1.0f));
	alFilterf_ptr(filter, AL_BANDPASS_GAINHF, Clamp(gain_hf, 0.0f, 1.0f));
}

void EFX_DeleteFilter(ALuint filter)
{
	if (!efx_state.initialized || filter == 0)
		return;

	alDeleteFilters_ptr(1, &filter);
}
