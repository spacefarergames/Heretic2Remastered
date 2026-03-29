//
// cd_audio.h -- CD audio playback via MCI.
//
// Copyright 2025
//

#pragma once

#include "q_Typedef.h"

// Initialize CD audio. Call after CD detection.
void CDAudio_Init(void);

// Shutdown CD audio.
void CDAudio_Shutdown(void);

// Play a CD audio track. Track numbers match the original Heretic II CD layout.
void CDAudio_Play(int track, qboolean looping);

// Stop CD audio playback.
void CDAudio_Stop(void);

// Update CD audio (check playback status, handle looping). Call every frame.
void CDAudio_Update(void);

// Returns true if a CD with audio tracks is available for playback.
qboolean CDAudio_IsActive(void);
