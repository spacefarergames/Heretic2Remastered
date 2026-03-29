//
// cl_mp4.c
//
// MP4 cinematic playback using Windows Media Foundation (IMFSourceReader).
// Produces BGRA video frames (MFVideoFormat_RGB32, bottom-up) and PCM audio
// that is pushed directly to se.RawSamples for the engine's sound mixer.
//
// Prerequisites: Windows 8+ (WMF H264 decoder is inbox on Win8+, available
// via codec pack on Win7).
//

#define COBJMACROS
#include <windows.h>
#include <objbase.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <mferror.h>

#pragma comment(lib, "mf.lib")
#pragma comment(lib, "mfplat.lib")
#pragma comment(lib, "mfreadwrite.lib")
#pragma comment(lib, "mfuuid.lib")
#pragma comment(lib, "ole32.lib")

#include "client.h"
#include "cl_mp4.h"

// ============================================================
// Internal state.
// ============================================================

#define MP4_AUDIO_ACCUM_SIZE (512 * 1024) // 512 KB per-frame audio accumulator

// Async loading states (accessed via InterlockedExchange).
#define MP4_LOAD_IDLE     0  // No load in progress.
#define MP4_LOAD_WORKING  1  // Background thread is running.
#define MP4_LOAD_OK       2  // Thread finished successfully; main thread must finalise.
#define MP4_LOAD_FAILED   3  // Thread finished with an error.

typedef struct MP4PlaybackInfo_s
{
	IMFSourceReader* reader;

	int     frame;
	float   fps;
	LONGLONG frame_duration; // 100-nanosecond units per video frame

	int     vid_width;
	int     vid_height;
	byte*   video_frame;     // BGRA, vid_width * vid_height * 4 bytes (bottom-up)

	int     snd_rate;
	int     snd_channels;
	int     snd_width;       // bytes per sample: 1 = 8-bit, 2 = 16-bit

	LONGLONG audio_timestamp; // last audio sample read position (100-ns)
	LONGLONG video_timestamp; // timestamp of the current video frame (100-ns)

	byte*   audio_accum;     // temporary accumulation buffer for one video frame's audio
	byte*   audio_leftover;      // excess audio bytes carried forward from previous frame
	DWORD   audio_leftover_size; // valid byte count in audio_leftover
	qboolean has_audio;
	qboolean at_end;
	qboolean valid;

	qboolean com_initialized; // true if this call to MP4_Open did CoInitializeEx

	// Async loading.
	volatile LONG load_state;     // MP4_LOAD_* constant
	HANDLE        load_thread;    // background thread handle
	char          load_filepath[MAX_OSPATH]; // filepath passed to the worker
} MP4PlaybackInfo_t;

static MP4PlaybackInfo_t mpi;

// ============================================================
// Helpers.
// ============================================================

static void ReadAudioForFrame(void)
{
	if (!mpi.has_audio || mpi.audio_accum == NULL || mpi.fps <= 0.0f)
		return;

	// Compute exactly how many PCM bytes correspond to one video frame.
	// Using bytes avoids WMF's presentation-timestamp scheme where each chunk's
	// ts marks its *start*, not its end, which causes the timestamp comparison to
	// over-read one frame and then starve the next (producing choppy output).
	const DWORD bytes_per_frame =
		(DWORD)((float)(mpi.snd_rate * mpi.snd_width * mpi.snd_channels) / mpi.fps + 0.5f);

	DWORD total_bytes = 0;

	// Prepend leftover bytes from the previous frame's overshoot.
	if (mpi.audio_leftover_size > 0 && mpi.audio_leftover != NULL)
	{
		memcpy(mpi.audio_accum, mpi.audio_leftover, mpi.audio_leftover_size);
		total_bytes = mpi.audio_leftover_size;
		mpi.audio_leftover_size = 0;
	}

	while (total_bytes < bytes_per_frame)
	{
		DWORD audio_flags = 0;
		LONGLONG audio_ts = 0;
		IMFSample* audio_sample = NULL;

		HRESULT hr = IMFSourceReader_ReadSample(
			mpi.reader,
			MF_SOURCE_READER_FIRST_AUDIO_STREAM,
			0, NULL, &audio_flags, &audio_ts, &audio_sample);

		if (FAILED(hr) || (audio_flags & MF_SOURCE_READERF_ENDOFSTREAM))
		{
			if (audio_sample) IMFSample_Release(audio_sample);
			break;
		}

		if (audio_sample == NULL)
			continue;

		IMFMediaBuffer* buf = NULL;
		if (SUCCEEDED(IMFSample_ConvertToContiguousBuffer(audio_sample, &buf)))
		{
			BYTE* data = NULL;
			DWORD len = 0;
			if (SUCCEEDED(IMFMediaBuffer_Lock(buf, &data, NULL, &len)))
			{
				if (total_bytes + len <= MP4_AUDIO_ACCUM_SIZE)
				{
					memcpy(mpi.audio_accum + total_bytes, data, len);
					total_bytes += len;
				}
				IMFMediaBuffer_Unlock(buf);
			}
			IMFMediaBuffer_Release(buf);
		}
		IMFSample_Release(audio_sample);
	}

	// Save any overshoot for the next frame to prevent cumulative A/V drift.
	if (total_bytes > bytes_per_frame && mpi.audio_leftover != NULL)
	{
		const DWORD excess = total_bytes - bytes_per_frame;
		memcpy(mpi.audio_leftover, mpi.audio_accum + bytes_per_frame, excess);
		mpi.audio_leftover_size = excess;
		total_bytes = bytes_per_frame;
	}

	if (total_bytes > 0)
	{
		const int samples = (int)(total_bytes / (DWORD)(mpi.snd_width * mpi.snd_channels));
		se.RawSamples(samples, mpi.snd_rate, mpi.snd_width, mpi.snd_channels,
			mpi.audio_accum, Cvar_VariableValue("s_volume"));
	}
}

// ============================================================
// Background loading thread.
// ============================================================

static DWORD WINAPI MP4_LoadThread(LPVOID param)
{
	(void)param;

	// Initialise COM on this thread (idempotent if already initialised).
	HRESULT hr = CoInitializeEx(NULL, COINIT_MULTITHREADED);
	mpi.com_initialized = (hr == S_OK); // Only uninit if we actually initialised it.

	if (FAILED(MFStartup(MF_VERSION, MFSTARTUP_NOSOCKET)))
	{
		if (mpi.com_initialized) CoUninitialize();
		InterlockedExchange(&mpi.load_state, MP4_LOAD_FAILED);
		return 1;
	}

	// Convert filepath to wide string for WMF.
	WCHAR wpath[MAX_OSPATH];
	MultiByteToWideChar(CP_ACP, 0, mpi.load_filepath, -1, wpath, MAX_OSPATH);

	// Enable automatic format conversion so the source reader will insert a
	// colour-space/pixel-format MFT that converts to MFVideoFormat_RGB32.
	// Without this attribute, SetCurrentMediaType rejects RGB32 if the decoder
	// doesn't output it natively.
	IMFAttributes* src_attrs = NULL;
	MFCreateAttributes(&src_attrs, 1);
	IMFAttributes_SetUINT32(src_attrs, &MF_SOURCE_READER_ENABLE_VIDEO_PROCESSING, TRUE);

	HRESULT create_hr = MFCreateSourceReaderFromURL(wpath, src_attrs, &mpi.reader);
	IMFAttributes_Release(src_attrs);

	if (FAILED(create_hr))
	{
		MFShutdown();
		if (mpi.com_initialized) CoUninitialize();
		InterlockedExchange(&mpi.load_state, MP4_LOAD_FAILED);
		return 1;
	}

	// ---- Configure video output: MFVideoFormat_RGB32 (BGRA, bottom-up) ----
	IMFMediaType* video_type = NULL;
	MFCreateMediaType(&video_type);
	IMFMediaType_SetGUID(video_type, &MF_MT_MAJOR_TYPE, &MFMediaType_Video);
	IMFMediaType_SetGUID(video_type, &MF_MT_SUBTYPE, &MFVideoFormat_RGB32);
	hr = IMFSourceReader_SetCurrentMediaType(mpi.reader,
		MF_SOURCE_READER_FIRST_VIDEO_STREAM, NULL, video_type);
	IMFMediaType_Release(video_type);

	if (FAILED(hr))
		goto fail;

	// Query actual video dimensions and frame rate.
	IMFMediaType* actual_video = NULL;
	if (FAILED(IMFSourceReader_GetCurrentMediaType(mpi.reader,
		MF_SOURCE_READER_FIRST_VIDEO_STREAM, &actual_video)))
		goto fail;

	// MFGetAttributeSize/MFGetAttributeRatio are C++ inline helpers; use GetUINT64
	// directly. Frame size packs: high32 = width, low32 = height. Frame rate packs:
	// high32 = numerator, low32 = denominator.
	UINT32 w = 0, h = 0;
	{ UINT64 fs = 0; IMFMediaType_GetUINT64(actual_video, &MF_MT_FRAME_SIZE, &fs); w = (UINT32)(fs >> 32); h = (UINT32)(fs & 0xFFFFFFFFu); }
	mpi.vid_width  = (int)w;
	mpi.vid_height = (int)h;

	// Prefer the native (container) media type for frame rate — the colour converter DSP
	// inserted by MF_SOURCE_READER_ENABLE_VIDEO_PROCESSING may not preserve MF_MT_FRAME_RATE.
	UINT32 fps_num = 0, fps_den = 1;
	{
		IMFMediaType* native_type = NULL;
		if (SUCCEEDED(IMFSourceReader_GetNativeMediaType(mpi.reader,
			MF_SOURCE_READER_FIRST_VIDEO_STREAM, 0, &native_type)))
		{
			UINT64 fr = 0;
			IMFMediaType_GetUINT64(native_type, &MF_MT_FRAME_RATE, &fr);
			fps_num = (UINT32)(fr >> 32);
			fps_den = (UINT32)(fr & 0xFFFFFFFFu);
			IMFMediaType_Release(native_type);
		}
		// Fallback: read from the converted output type.
		if (fps_num == 0 || fps_den == 0)
		{
			UINT64 fr = 0;
			IMFMediaType_GetUINT64(actual_video, &MF_MT_FRAME_RATE, &fr);
			fps_num = (UINT32)(fr >> 32);
			fps_den = (UINT32)(fr & 0xFFFFFFFFu);
		}
	}
	IMFMediaType_Release(actual_video);

	if (fps_num == 0 || fps_den == 0 || mpi.vid_width == 0 || mpi.vid_height == 0)
		goto fail;

	mpi.fps = (float)fps_num / (float)fps_den;
	mpi.frame_duration = (LONGLONG)(10000000.0 * fps_den / fps_num);

	mpi.video_frame = malloc((size_t)(mpi.vid_width * mpi.vid_height * 4));
	if (mpi.video_frame == NULL)
		goto fail;

	// ---- Configure audio output: PCM ----
	IMFMediaType* audio_type = NULL;
	MFCreateMediaType(&audio_type);
	IMFMediaType_SetGUID(audio_type, &MF_MT_MAJOR_TYPE, &MFMediaType_Audio);
	IMFMediaType_SetGUID(audio_type, &MF_MT_SUBTYPE, &MFAudioFormat_PCM);
	hr = IMFSourceReader_SetCurrentMediaType(mpi.reader,
		MF_SOURCE_READER_FIRST_AUDIO_STREAM, NULL, audio_type);
	IMFMediaType_Release(audio_type);

	mpi.has_audio = SUCCEEDED(hr);
	if (mpi.has_audio)
	{
		IMFMediaType* actual_audio = NULL;
		if (SUCCEEDED(IMFSourceReader_GetCurrentMediaType(mpi.reader,
			MF_SOURCE_READER_FIRST_AUDIO_STREAM, &actual_audio)))
		{
			UINT32 rate = 0, channels = 0, bits = 0;
			IMFMediaType_GetUINT32(actual_audio, &MF_MT_AUDIO_SAMPLES_PER_SECOND, &rate);
			IMFMediaType_GetUINT32(actual_audio, &MF_MT_AUDIO_NUM_CHANNELS, &channels);
			IMFMediaType_GetUINT32(actual_audio, &MF_MT_AUDIO_BITS_PER_SAMPLE, &bits);
			IMFMediaType_Release(actual_audio);

			mpi.snd_rate     = (int)rate;
			mpi.snd_channels = (int)max(1, channels);
			mpi.snd_width    = (int)(bits / 8);
		}
		else
		{
			mpi.has_audio = false;
		}

		mpi.audio_accum = malloc(MP4_AUDIO_ACCUM_SIZE);
		if (mpi.audio_accum == NULL)
			mpi.has_audio = false;

		mpi.audio_leftover = malloc(MP4_AUDIO_ACCUM_SIZE);
		if (mpi.audio_leftover == NULL)
			mpi.has_audio = false;
		mpi.audio_leftover_size = 0;
	}

	// Signal success — main thread will finalise (DrawInitCinematic, first frame).
	InterlockedExchange(&mpi.load_state, MP4_LOAD_OK);
	return 0;

fail:
	if (mpi.reader)        { IMFSourceReader_Release(mpi.reader); mpi.reader = NULL; }
	if (mpi.video_frame)   { free(mpi.video_frame); mpi.video_frame = NULL; }
	if (mpi.audio_accum)   { free(mpi.audio_accum); mpi.audio_accum = NULL; }
	if (mpi.audio_leftover){ free(mpi.audio_leftover); mpi.audio_leftover = NULL; }
	MFShutdown();
	if (mpi.com_initialized) CoUninitialize();
	InterlockedExchange(&mpi.load_state, MP4_LOAD_FAILED);
	return 1;
}

// ============================================================
// Public API.
// ============================================================

qboolean MP4_Open(const char* filepath)
{
	memset(&mpi, 0, sizeof(mpi));
	strncpy(mpi.load_filepath, filepath, MAX_OSPATH - 1);
	mpi.load_filepath[MAX_OSPATH - 1] = '\0';

	InterlockedExchange(&mpi.load_state, MP4_LOAD_WORKING);

	mpi.load_thread = CreateThread(NULL, 0, MP4_LoadThread, NULL, 0, NULL);
	if (mpi.load_thread == NULL)
	{
		InterlockedExchange(&mpi.load_state, MP4_LOAD_IDLE);
		Com_Printf("MP4_Open: CreateThread failed.\n");
		return false;
	}

	Com_Printf("MP4_Open: loading '%s' on background thread...\n", filepath);
	return true;
}

qboolean MP4_FinishOpen(void)
{
	if (mpi.load_thread != NULL)
	{
		WaitForSingleObject(mpi.load_thread, INFINITE);
		CloseHandle(mpi.load_thread);
		mpi.load_thread = NULL;
	}

	const LONG state = InterlockedCompareExchange(&mpi.load_state, MP4_LOAD_IDLE, MP4_LOAD_IDLE);
	if (state != MP4_LOAD_OK)
	{
		Com_Printf("MP4_Open: background load failed for '%s'.\n", mpi.load_filepath);
		memset(&mpi, 0, sizeof(mpi));
		return false;
	}

	mpi.valid = true;

	re.DrawInitCinematic(mpi.vid_width, mpi.vid_height);

	Com_Printf("MP4_Open: %dx%d @ %.1f fps, audio %s.\n",
		mpi.vid_width, mpi.vid_height, mpi.fps,
		mpi.has_audio ? "on" : "off");

	return true;
}

void MP4_Shutdown(void)
{
	// If a background load is still running, wait for it to finish.
	if (mpi.load_thread != NULL)
	{
		WaitForSingleObject(mpi.load_thread, INFINITE);
		CloseHandle(mpi.load_thread);
		mpi.load_thread = NULL;
	}

	if (!mpi.valid)
	{
		// Thread may have allocated WMF resources before being shut down.
		const LONG state = InterlockedCompareExchange(&mpi.load_state, MP4_LOAD_IDLE, MP4_LOAD_IDLE);
		if (state == MP4_LOAD_OK || state == MP4_LOAD_FAILED)
		{
			if (mpi.reader)        { IMFSourceReader_Release(mpi.reader); mpi.reader = NULL; }
			if (mpi.video_frame)   { free(mpi.video_frame); mpi.video_frame = NULL; }
			if (mpi.audio_accum)   { free(mpi.audio_accum); mpi.audio_accum = NULL; }
			if (mpi.audio_leftover){ free(mpi.audio_leftover); mpi.audio_leftover = NULL; }
			if (state == MP4_LOAD_OK) { MFShutdown(); if (mpi.com_initialized) CoUninitialize(); }
		}
		memset(&mpi, 0, sizeof(mpi));
		return;
	}

	re.DrawCloseCinematic();

	if (mpi.reader)        { IMFSourceReader_Release(mpi.reader); mpi.reader = NULL; }
	if (mpi.video_frame)   { free(mpi.video_frame); mpi.video_frame = NULL; }
	if (mpi.audio_accum)   { free(mpi.audio_accum); mpi.audio_accum = NULL; }
	if (mpi.audio_leftover){ free(mpi.audio_leftover); mpi.audio_leftover = NULL; }

	MFShutdown();
	if (mpi.com_initialized)
		CoUninitialize();

	memset(&mpi, 0, sizeof(mpi));
}

qboolean MP4_IsOpen(void)    { return mpi.valid; }
qboolean MP4_IsLoading(void) { return InterlockedCompareExchange(&mpi.load_state, MP4_LOAD_IDLE, MP4_LOAD_IDLE) == MP4_LOAD_WORKING; }
qboolean MP4_AtEnd(void)     { return mpi.at_end; }
int      MP4_GetWidth(void)  { return mpi.vid_width; }
int      MP4_GetHeight(void) { return mpi.vid_height; }
float    MP4_GetFPS(void)    { return mpi.fps > 0.0f ? mpi.fps : 15.0f; }
int      MP4_GetFrame(void)  { return mpi.frame; }

const byte* MP4_GetVideoFrame(void) { return mpi.video_frame; }

void MP4_NextFrame(void)
{
	if (!mpi.valid || mpi.at_end || mpi.reader == NULL)
		return;

	DWORD flags = 0;
	LONGLONG ts = 0;
	IMFSample* sample = NULL;

	HRESULT hr = IMFSourceReader_ReadSample(
		mpi.reader,
		MF_SOURCE_READER_FIRST_VIDEO_STREAM,
		0, NULL, &flags, &ts, &sample);

	if (FAILED(hr) || (flags & MF_SOURCE_READERF_ENDOFSTREAM))
	{
		mpi.at_end = true;
		if (sample) IMFSample_Release(sample);
		return;
	}

	if (sample == NULL)
		return; // Stream tick with no data; caller will retry next frame.

	// Decode the compressed buffer to a contiguous BGRA block.
	IMFMediaBuffer* buf = NULL;
	if (SUCCEEDED(IMFSample_ConvertToContiguousBuffer(sample, &buf)))
	{
		BYTE* data = NULL;
		DWORD len = 0;
		if (SUCCEEDED(IMFMediaBuffer_Lock(buf, &data, NULL, &len)))
		{
			const int expected = mpi.vid_width * mpi.vid_height * 4;
			if ((int)len >= expected)
				memcpy(mpi.video_frame, data, expected);
			IMFMediaBuffer_Unlock(buf);
		}
		IMFMediaBuffer_Release(buf);
	}
	IMFSample_Release(sample);

	mpi.video_timestamp = ts;
	mpi.frame++;

	// Push audio samples that align with this video frame's time window.
	ReadAudioForFrame();
}
