//
// cd_audio.c -- CD audio playback via MCI (Media Control Interface).
//
// Copyright 2025
//

#include <windows.h>
#include <mmsystem.h>
#include "qcommon.h"
#include "cd_audio.h"
#include "cd_detect.h"

static qboolean cd_initialized = false;
static qboolean cd_playing = false;
static qboolean cd_looping = false;
static int cd_current_track = 0;
static MCIDEVICEID cd_device = 0;

static cvar_t* cd_volume;

void CDAudio_Init(void)
{
	cd_volume = Cvar_Get("cd_volume", "0.5", CVAR_ARCHIVE);

	const char* cd_path = CD_GetDrivePath();
	if (cd_path == NULL)
		return;

	// Open the CD audio device on the detected drive.
	MCI_OPEN_PARMS open_parms;
	memset(&open_parms, 0, sizeof(open_parms));
	open_parms.lpstrDeviceType = "cdaudio";

	// Build drive specifier (e.g., "D:").
	char drive_spec[4];
	drive_spec[0] = cd_path[0];
	drive_spec[1] = ':';
	drive_spec[2] = '\0';
	open_parms.lpstrElementName = drive_spec;

	DWORD result = mciSendCommand(0, MCI_OPEN, MCI_OPEN_TYPE | MCI_OPEN_ELEMENT, (DWORD_PTR)&open_parms);
	if (result != 0)
	{
		// Try without specifying the element (uses default CD drive).
		result = mciSendCommand(0, MCI_OPEN, MCI_OPEN_TYPE, (DWORD_PTR)&open_parms);
		if (result != 0)
		{
			char err_buf[256];
			mciGetErrorString(result, err_buf, sizeof(err_buf));
			Com_Printf("CDAudio_Init: MCI open failed: %s\n", err_buf);

			return;
		}
	}

	cd_device = open_parms.wDeviceID;

	// Set time format to tracks.
	MCI_SET_PARMS set_parms;
	memset(&set_parms, 0, sizeof(set_parms));
	set_parms.dwTimeFormat = MCI_FORMAT_TMSF;

	result = mciSendCommand(cd_device, MCI_SET, MCI_SET_TIME_FORMAT, (DWORD_PTR)&set_parms);
	if (result != 0)
	{
		char err_buf[256];
		mciGetErrorString(result, err_buf, sizeof(err_buf));
		Com_Printf("CDAudio_Init: MCI set time format failed: %s\n", err_buf);

		mciSendCommand(cd_device, MCI_CLOSE, 0, 0);
		cd_device = 0;

		return;
	}

	cd_initialized = true;
	Com_Printf("CD Audio initialized.\n");
}

void CDAudio_Shutdown(void)
{
	if (!cd_initialized)
		return;

	CDAudio_Stop();

	if (cd_device != 0)
	{
		mciSendCommand(cd_device, MCI_CLOSE, 0, 0);
		cd_device = 0;
	}

	cd_initialized = false;
}

void CDAudio_Play(const int track, const qboolean looping)
{
	if (!cd_initialized || cd_device == 0)
		return;

	// Stop any current playback.
	if (cd_playing)
		CDAudio_Stop();

	// Verify the track is an audio track (not data).
	MCI_STATUS_PARMS status_parms;
	memset(&status_parms, 0, sizeof(status_parms));
	status_parms.dwItem = MCI_CDA_STATUS_TYPE_TRACK;
	status_parms.dwTrack = track;

	DWORD result = mciSendCommand(cd_device, MCI_STATUS, MCI_STATUS_ITEM | MCI_TRACK, (DWORD_PTR)&status_parms);
	if (result != 0)
	{
		Com_DPrintf("CDAudio_Play: MCI status failed for track %i\n", track);
		return;
	}

	if (status_parms.dwReturn != MCI_CDA_TRACK_AUDIO)
	{
		Com_DPrintf("CDAudio_Play: track %i is not an audio track\n", track);
		return;
	}

	// Get the total number of tracks to determine end position.
	MCI_STATUS_PARMS num_tracks_parms;
	memset(&num_tracks_parms, 0, sizeof(num_tracks_parms));
	num_tracks_parms.dwItem = MCI_STATUS_NUMBER_OF_TRACKS;

	result = mciSendCommand(cd_device, MCI_STATUS, MCI_STATUS_ITEM, (DWORD_PTR)&num_tracks_parms);
	if (result != 0)
		return;

	// Play the track.
	MCI_PLAY_PARMS play_parms;
	memset(&play_parms, 0, sizeof(play_parms));
	play_parms.dwFrom = MCI_MAKE_TMSF(track, 0, 0, 0);

	if ((DWORD)track < num_tracks_parms.dwReturn)
		play_parms.dwTo = MCI_MAKE_TMSF(track + 1, 0, 0, 0);

	result = mciSendCommand(cd_device, MCI_PLAY, MCI_FROM | ((DWORD)track < num_tracks_parms.dwReturn ? MCI_TO : 0), (DWORD_PTR)&play_parms);
	if (result != 0)
	{
		char err_buf[256];
		mciGetErrorString(result, err_buf, sizeof(err_buf));
		Com_DPrintf("CDAudio_Play: MCI play failed: %s\n", err_buf);

		return;
	}

	cd_playing = true;
	cd_looping = looping;
	cd_current_track = track;
}

void CDAudio_Stop(void)
{
	if (!cd_initialized || !cd_playing)
		return;

	mciSendCommand(cd_device, MCI_STOP, 0, 0);
	cd_playing = false;
	cd_looping = false;
	cd_current_track = 0;
}

void CDAudio_Update(void)
{
	if (!cd_initialized || !cd_playing)
		return;

	// Check if playback is still active.
	MCI_STATUS_PARMS status_parms;
	memset(&status_parms, 0, sizeof(status_parms));
	status_parms.dwItem = MCI_STATUS_MODE;

	DWORD result = mciSendCommand(cd_device, MCI_STATUS, MCI_STATUS_ITEM, (DWORD_PTR)&status_parms);
	if (result != 0)
		return;

	// If playback has stopped and we're looping, restart the track.
	if (status_parms.dwReturn != MCI_MODE_PLAY)
	{
		cd_playing = false;

		if (cd_looping && cd_current_track > 0)
		{
			CDAudio_Play(cd_current_track, true);
		}
		else
		{
			cd_looping = false;
			cd_current_track = 0;
		}
	}

	// Note: Volume control via MCI is limited and depends on the CD-ROM drive.
	// Modern systems may not support this functionality, so we don't attempt to set it here.
	// The cd_volume cvar is kept for potential future use or compatibility.
}

qboolean CDAudio_IsActive(void)
{
	return cd_initialized;
}
