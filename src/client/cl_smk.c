//
// cl_smk.c -- libsmacker interface (https://github.com/JonnyH/libsmacker)
//
// Copyright 1998 Raven Software
//

#include "client.h"
#include "cl_mp4.h"
#include <libsmacker/smacker.h>

static char cin_temp_file[MAX_OSPATH]; // Temp file path for PAK-extracted video.

typedef struct SMKPlaybackInfo_s
{
	smk smk_obj;

	int frame;
	int total_frames;

	const byte* video_frame;
	const byte* palette;
	float fps; //mxd. Original logic uses cin_rate cvar instead of this.

	// Current .smk video properties.
	int vid_width;
	int vid_height;

	// Current .smk audio properties.
	int snd_channels;
	int snd_width;
	int snd_rate;

	// Auxiliary sound buffer...
	byte* audio_buffer;
	int audio_buffer_size;
	int audio_buffer_end;
} SMKPlaybackInfo_t;

static SMKPlaybackInfo_t spi;

static qboolean SMK_Open(const char* name)
{
	spi.frame = 0; // Reset frame counter.

	spi.smk_obj = smk_open_file(name, SMK_MODE_DISK);
	if (spi.smk_obj == NULL)
		return false;

	smk_enable_all(spi.smk_obj, SMK_VIDEO_TRACK | SMK_AUDIO_TRACK_0);

	ulong frame_count;
	double usf; // Microseconds per frame.
	smk_info_all(spi.smk_obj, NULL, &frame_count, &usf);

	spi.total_frames = (int)frame_count;
	spi.fps = floorf(1000000.0f / (float)usf);

	smk_info_video(spi.smk_obj, &spi.vid_width, &spi.vid_height, NULL);

	byte s_channels[7];
	byte s_bitdepth[7];
	ulong s_rate[7];
	smk_info_audio(spi.smk_obj, NULL, s_channels, s_bitdepth, s_rate);

	spi.snd_channels = s_channels[0];
	spi.snd_width = s_bitdepth[0] / 8; // s_bitdepth: 8 or 16.
	spi.snd_rate = (int)s_rate[0];

	smk_first(spi.smk_obj);
	spi.palette = smk_get_palette(spi.smk_obj);

	if ((spi.vid_width & 7) != 0 || (spi.vid_height & 7) != 0)
	{
		Com_Printf("...Smacker file must but a multiple of 8 high and wide\n");
		SMK_Shutdown();

		return false;
	}

	re.DrawInitCinematic(spi.vid_width, spi.vid_height);

	return true;
}

void SMK_Shutdown(void)
{
	if (spi.smk_obj != NULL)
	{
		re.DrawCloseCinematic();

		smk_close(spi.smk_obj);
		spi.smk_obj = NULL;

		spi.video_frame = NULL;
		spi.palette = NULL;

		if (spi.audio_buffer != NULL)
		{
			free(spi.audio_buffer);
			spi.audio_buffer = NULL;
		}
	}
}

static void SCR_DoCinematicFrame(void) // Called when it's time to render next cinematic frame (e.g. at 15 fps).
{
	// Grab video frame.
	spi.video_frame = smk_get_video(spi.smk_obj);

	// Grab audio frame (way more involved than you may expect)...
	const int smk_audio_frame_size = (int)smk_get_audio_size(spi.smk_obj, 0);

	//mxd. The first smk frame contains 16 frames of audio data. This will overflow s_rawsamples[] if used as is, resulting in desynched audio, so use an auxiliary buffer...
	//mxd. Interestingly, official RAD Video Tools (and SmackW32.dll bundled with vanilla H2) start audio playback from 2-nd frame & end 1 frame too early.
	if (spi.frame == 0)
	{
		spi.audio_buffer = malloc(smk_audio_frame_size);
		spi.audio_buffer_size = smk_audio_frame_size;
		spi.audio_buffer_end = 0;
	}

	// Calculate audio frame size.
	int frame_size;

	//mxd. On frame 0, smk_audio_frame_size is 16 times bigger than needed. Zero smk_audio_frame_size means we have to use remaining buffered audio frames.
	if (spi.frame == 0 || smk_audio_frame_size == 0)
		frame_size = (int)((float)(spi.snd_rate * spi.snd_width) / spi.fps); // Calculate actual frame size.
	else
		frame_size = smk_audio_frame_size; // Use provided frame size.

	// Store audio frame in auxiliary buffer...
	memcpy(spi.audio_buffer + spi.audio_buffer_end, smk_get_audio(spi.smk_obj, 0), smk_audio_frame_size);
	spi.audio_buffer_end += smk_audio_frame_size;

	// Upload audio chunk RawSamples() can handle...
	const int num_samples = frame_size / (spi.snd_width * spi.snd_channels);
	se.RawSamples(num_samples, spi.snd_rate, spi.snd_width, spi.snd_channels, spi.audio_buffer, Cvar_VariableValue("s_volume"));

	// Remove uploaded audio chunk...
	memmove_s(spi.audio_buffer, spi.audio_buffer_size, spi.audio_buffer + frame_size, spi.audio_buffer_size - frame_size);
	spi.audio_buffer_end -= frame_size;

	assert(spi.audio_buffer_end >= 0);

	// Go to next frame.
	smk_next(spi.smk_obj);
	spi.frame++;
}

void SCR_PlayCinematic(const char* name)
{
	//mxd. SCR_BeginLoadingPlaque() in original logic.
	se.StopAllSounds_Sounding();
	se.MusicStop();

	// Try HD video replacements (MP4, then MKV) from the HDVideos folder.
	const char* dot = strrchr(name, '.');
	const int base_len = dot ? (int)(dot - name) : (int)strlen(name);

	static const char* hd_exts[] = { "mp4", "mkv", NULL };

	for (int e = 0; hd_exts[e] != NULL; e++)
	{
		char hd_name[MAX_OSPATH];
		sprintf_s(hd_name, sizeof(hd_name), "%.*s.%s", base_len, name, hd_exts[e]);

		char hd_relpath[MAX_OSPATH];
		sprintf_s(hd_relpath, sizeof(hd_relpath), "HDVideos/%s", hd_name);

		// First try finding as a loose file on disk.
		const char* hd_basepath = FS_GetPath(hd_relpath);

		if (hd_basepath != NULL)
		{
			char hd_filepath[MAX_OSPATH];
			sprintf_s(hd_filepath, sizeof(hd_filepath), "%s/HDVideos/%s", hd_basepath, hd_name);
			Com_Printf("Opening HD cinematic: '%s'...\n", hd_filepath);

			if (MP4_Open(hd_filepath))
				{
					// Background thread is loading; enter cinematic state now.
					// SCR_RunCinematic will call MP4_FinishOpen and prime the first frame.
					cl.cinematictime = max(1, cls.realtime);
					Cvar_SetValue("paused", 0);
					cls.state = ca_connected;
					SCR_EndLoadingPlaque();
					In_FlushQueue();
					cls.key_dest = key_game;
					return;
				}
		}

		// Try loading from PAK filesystem (extract to temp file for WMF).
		void* data;
		const int data_len = FS_LoadFile(hd_relpath, &data);

		if (data != NULL && data_len > 0)
		{
			// Write to a temp file in the game directory.
			sprintf_s(cin_temp_file, sizeof(cin_temp_file), "%s/cin_temp.%s", FS_Gamedir(), hd_exts[e]);

			FILE* tmp;
			if (fopen_s(&tmp, cin_temp_file, "wb") == 0)
			{
				fwrite(data, 1, data_len, tmp);
				fclose(tmp);

				Com_Printf("Opening HD cinematic from PAK: '%s'...\n", hd_relpath);

				if (MP4_Open(cin_temp_file))
					{
						FS_FreeFile(data);
						// Background thread is loading; enter cinematic state now.
						cl.cinematictime = max(1, cls.realtime);
						Cvar_SetValue("paused", 0);
						cls.state = ca_connected;
						SCR_EndLoadingPlaque();
						In_FlushQueue();
						cls.key_dest = key_game;
						return;
					}

				// MP4_Open failed, clean up temp file.
				remove(cin_temp_file);
				cin_temp_file[0] = '\0';
			}

			FS_FreeFile(data);
		}
	}

	// No HD video found; skip this cinematic.
	Com_Printf("No HD video found for '%s', skipping cinematic.\n", name);
	SCR_FinishCinematic();
}

void SCR_DrawCinematic(void) // Called every rendered frame.
{
	if (cl.cinematictime <= 0)
		return;

	if (MP4_IsOpen())
		re.DrawCinematic(MP4_GetVideoFrame(), NULL); // NULL palette = BGRA MP4 frame
	// While MP4_IsLoading(), we just show whatever the engine draws (black).
}

void SCR_RunCinematic(void) // Called every rendered frame.
{
	if (cl.cinematictime < 1)
		return;

	// Background thread still loading — keep the frame loop alive but don't advance.
	if (MP4_IsLoading())
		return;

	// Background load finished — finalize on the main thread (GPU resources).
	if (!MP4_IsOpen())
	{
		if (!MP4_FinishOpen())
		{
			SCR_FinishCinematic();
			return;
		}

		// Set cinematic time now that FPS is available, and prime the first frame.
		cl.cinematictime = (int)((float)cls.realtime - 2000.0f / MP4_GetFPS());
		cl.cinematictime = max(1, cl.cinematictime);
		MP4_NextFrame();
		return;
	}

	const float fps = MP4_GetFPS();
	const int   cur = MP4_GetFrame();

	// Pause if menu or console is up.
	if (cls.key_dest != key_game)
	{
		cl.cinematictime = (int)((float)cls.realtime - (float)(cur * 1000) / fps);
		return;
	}

	if (MP4_AtEnd())
	{
		SCR_FinishCinematic();
		return;
	}

	const int frame = (int)((float)(cls.realtime - cl.cinematictime) * fps / 1000.0f);
	if (frame <= cur)
		return;

	if (frame > cur + 1)
		cl.cinematictime = (int)((float)cls.realtime - (float)(cur * 1000) / fps);

	MP4_NextFrame();
}

void SCR_StopCinematic(void)
{
	cl.cinematictime = 0; // Done
	if (MP4_IsOpen() || MP4_IsLoading())
		MP4_Shutdown();

	// Clean up temp file extracted from PAK.
	if (cin_temp_file[0] != '\0')
	{
		remove(cin_temp_file);
		cin_temp_file[0] = '\0';
	}
}

// Called when either the cinematic completes, or it is aborted.
void SCR_FinishCinematic(void)
{
	// Tell the server to advance to the next map / cinematic.
	MSG_WriteByte(&cls.netchan.message, clc_stringcmd);
	SZ_Print(&cls.netchan.message, va("nextserver %i\n", cl.servercount));

	SCR_StopCinematic();
}