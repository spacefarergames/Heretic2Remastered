//
// ce_DLight.c
//
// Copyright 1998 Raven Software
//

#include "ce_DLight.h"
#include "Client Entities.h"
#include "ResourceManager.h"
#include "Vector.h"

static ResourceManager_t dlight_manager;

void InitDLightMngr(void)
{
#define DLIGHT_BLOCK_SIZE 32

	ResMngr_Con(&dlight_manager, sizeof(CE_DLight_t), DLIGHT_BLOCK_SIZE);
}

void ReleaseDLightMngr(void)
{
	ResMngr_Des(&dlight_manager);
}

struct CE_DLight_s* CE_DLight_new(const paletteRGBA_t color, const float intensity, const float d_intensity)
{
	CE_DLight_t* dl = ResMngr_AllocateResource(&dlight_manager, sizeof(*dl));

	dl->color = color;
	dl->intensity = intensity;
	dl->d_intensity = d_intensity;
	dl->Update = NULL; //mxd

	//mxd. Disable color-fading.
	dl->fade_start_time = 0;
	dl->fade_end_time = 0;
	VectorClear(dl->fade_color_start);
	VectorClear(dl->fade_color_end);

	//mxd. Disable flicker by default.
	dl->flicker_base_intensity = 0.0f;
	dl->flicker_amplitude = 0.0f;
	dl->flicker_speed = 0.0f;

	return dl;
}

void CE_DLight_delete(struct CE_DLight_s* to_delete)
{
	ResMngr_DeallocateResource(&dlight_manager, to_delete, sizeof(*to_delete));
}

//mxd. Set dlight color interpolation. end_r/g/b: color value in 0 .. 255 range, duration: fade duration (in ms.).
void CE_DLight_SetColorFade(struct CE_DLight_s* self, const float end_r, const float end_g, const float end_b, const int duration)
{
	assert(duration > 0 && end_r >= 0.0f && end_r <= 255.0f && end_g >= 0.0f && end_g <= 255.0f && end_b >= 0.0f && end_b <= 255.0f);

	// Runtime validation for release builds.
	if (duration <= 0 || end_r < 0.0f || end_r > 255.0f || end_g < 0.0f || end_g > 255.0f || end_b < 0.0f || end_b > 255.0f)
		return;

	self->fade_start_time = fx_time;
	self->fade_end_time = fx_time + duration;
	VectorSet(self->fade_color_start, self->color.r, self->color.g, self->color.b);
	VectorSet(self->fade_color_end, end_r, end_g, end_b);
}

//mxd. Set dlight flicker parameters. amplitude: flicker intensity variation (e.g. 0.2 = 20%), speed: flicker frequency multiplier.
void CE_DLight_SetFlicker(struct CE_DLight_s* self, const float amplitude, const float speed)
{
	self->flicker_base_intensity = self->intensity;
	self->flicker_amplitude = amplitude;
	self->flicker_speed = speed;
}