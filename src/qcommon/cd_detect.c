//
// cd_detect.c -- Heretic II CD detection and PAK file extraction from ZIP.
//
// Copyright 2025
//

#include <windows.h>
#include <stdio.h>
#include <string.h>
#include "qcommon.h"
#include "cd_detect.h"

static char cd_drive_path[8]; // e.g., "D:\"
static qboolean cd_detected = false;

#pragma region ========================== INFLATE (RFC 1951) ==========================

// Minimal DEFLATE decompressor for extracting files from ZIP archives.

#define INF_MAXBITS		15
#define INF_MAXLCODES	286
#define INF_MAXDCODES	30
#define INF_MAXCODES	(INF_MAXLCODES + INF_MAXDCODES)
#define INF_FIXLCODES	288

typedef struct
{
	const byte* in;
	int in_len;
	int in_pos;

	byte* out;
	int out_len;
	int out_pos;

	unsigned int bitbuf;
	int bitcnt;
} inf_state_t;

typedef struct
{
	short count[INF_MAXBITS + 1];
	short symbol[INF_MAXCODES];
} inf_huffman_t;

static int Inf_GetBits(inf_state_t* s, const int need)
{
	while (s->bitcnt < need)
	{
		if (s->in_pos >= s->in_len)
			return -1;

		s->bitbuf |= (unsigned int)s->in[s->in_pos++] << s->bitcnt;
		s->bitcnt += 8;
	}

	const int val = (int)(s->bitbuf & ((1u << need) - 1));
	s->bitbuf >>= need;
	s->bitcnt -= need;

	return val;
}

static int Inf_BuildHuffman(inf_huffman_t* h, const short* lengths, const int num_symbols)
{
	// Count number of codes of each length.
	for (int i = 0; i <= INF_MAXBITS; i++)
		h->count[i] = 0;

	for (int i = 0; i < num_symbols; i++)
		h->count[lengths[i]]++;

	if (h->count[0] == num_symbols)
		return 0; // All zero-length codes (no codes).

	// Check for an over- or under-subscribed code set.
	int left = 1;
	for (int i = 1; i <= INF_MAXBITS; i++)
	{
		left <<= 1;
		left -= h->count[i];

		if (left < 0)
			return -1; // Over-subscribed.
	}

	// Generate offsets into symbol table for each length for sorting.
	short offs[INF_MAXBITS + 1];
	offs[1] = 0;
	for (int i = 1; i < INF_MAXBITS; i++)
		offs[i + 1] = offs[i] + h->count[i];

	// Put symbols in table sorted by length, by symbol order within each length.
	for (int i = 0; i < num_symbols; i++)
		if (lengths[i] != 0)
			h->symbol[offs[lengths[i]]++] = (short)i;

	return left;
}

static int Inf_Decode(inf_state_t* s, const inf_huffman_t* h)
{
	int code = 0;
	int first = 0;
	int index = 0;

	for (int len = 1; len <= INF_MAXBITS; len++)
	{
		const int bit = Inf_GetBits(s, 1);
		if (bit < 0)
			return -1;

		code |= bit;
		const int count = h->count[len];

		if (code - count < first)
			return h->symbol[index + (code - first)];

		index += count;
		first += count;
		first <<= 1;
		code <<= 1;
	}

	return -1; // Ran out of codes.
}

// Length base values.
static const short inf_len_base[] =
{
	3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27, 31,
	35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258
};

// Length extra bits.
static const short inf_len_extra[] =
{
	0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2,
	3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0
};

// Distance base values.
static const short inf_dist_base[] =
{
	1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129, 193,
	257, 385, 513, 769, 1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577
};

// Distance extra bits.
static const short inf_dist_extra[] =
{
	0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6,
	7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13
};

// Order of code length code lengths.
static const short inf_clcl_order[] =
{
	16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15
};

static int Inf_DecompressCodes(inf_state_t* s, const inf_huffman_t* lencode, const inf_huffman_t* distcode)
{
	int symbol;

	do
	{
		symbol = Inf_Decode(s, lencode);
		if (symbol < 0)
			return -1;

		if (symbol < 256)
		{
			// Literal byte.
			if (s->out_pos >= s->out_len)
				return -1;

			s->out[s->out_pos++] = (byte)symbol;
		}
		else if (symbol > 256)
		{
			// Length/distance pair.
			symbol -= 257;
			if (symbol >= 29)
				return -1;

			int len = inf_len_base[symbol] + Inf_GetBits(s, inf_len_extra[symbol]);

			symbol = Inf_Decode(s, distcode);
			if (symbol < 0)
				return -1;

			int dist = inf_dist_base[symbol] + Inf_GetBits(s, inf_dist_extra[symbol]);

			if (dist > s->out_pos)
				return -1;

			// Copy match.
			while (len-- > 0)
			{
				if (s->out_pos >= s->out_len)
					return -1;

				s->out[s->out_pos] = s->out[s->out_pos - dist];
				s->out_pos++;
			}
		}
	} while (symbol != 256); // End of block marker.

	return 0;
}

static int Inf_FixedBlock(inf_state_t* s)
{
	static qboolean built = false;
	static inf_huffman_t lencode;
	static inf_huffman_t distcode;

	if (!built)
	{
		short lengths[INF_FIXLCODES];

		// Build fixed literal/length table: 0-143=8, 144-255=9, 256-279=7, 280-287=8.
		int i;
		for (i = 0; i < 144; i++) lengths[i] = 8;
		for (; i < 256; i++) lengths[i] = 9;
		for (; i < 280; i++) lengths[i] = 7;
		for (; i < INF_FIXLCODES; i++) lengths[i] = 8;

		Inf_BuildHuffman(&lencode, lengths, INF_FIXLCODES);

		// Build fixed distance table: all 5 bits.
		for (i = 0; i < INF_MAXDCODES; i++) lengths[i] = 5;
		Inf_BuildHuffman(&distcode, lengths, INF_MAXDCODES);

		built = true;
	}

	return Inf_DecompressCodes(s, &lencode, &distcode);
}

static int Inf_DynamicBlock(inf_state_t* s)
{
	short lengths[INF_MAXCODES];
	inf_huffman_t lencode;
	inf_huffman_t distcode;

	// Get the number of literal/length and distance codes.
	const int nlen = Inf_GetBits(s, 5) + 257;
	const int ndist = Inf_GetBits(s, 5) + 1;
	const int ncode = Inf_GetBits(s, 4) + 4;

	if (nlen > INF_MAXLCODES || ndist > INF_MAXDCODES)
		return -1;

	// Read code length code lengths.
	for (int i = 0; i < ncode; i++)
		lengths[inf_clcl_order[i]] = (short)Inf_GetBits(s, 3);

	for (int i = ncode; i < 19; i++)
		lengths[inf_clcl_order[i]] = 0;

	// Build code length code.
	inf_huffman_t clcode;
	if (Inf_BuildHuffman(&clcode, lengths, 19) != 0)
		return -1;

	// Read literal/length + distance code lengths.
	int index = 0;
	while (index < nlen + ndist)
	{
		int symbol = Inf_Decode(s, &clcode);
		if (symbol < 0)
			return -1;

		if (symbol < 16)
		{
			lengths[index++] = (short)symbol;
		}
		else
		{
			short len = 0;
			int times;

			if (symbol == 16)
			{
				if (index == 0) return -1;
				len = lengths[index - 1];
				times = 3 + Inf_GetBits(s, 2);
			}
			else if (symbol == 17)
			{
				times = 3 + Inf_GetBits(s, 3);
			}
			else // symbol == 18
			{
				times = 11 + Inf_GetBits(s, 7);
			}

			if (index + times > nlen + ndist)
				return -1;

			while (times-- > 0)
				lengths[index++] = len;
		}
	}

	// Build Huffman tables.
	if (Inf_BuildHuffman(&lencode, lengths, nlen) != 0)
		return -1;

	if (Inf_BuildHuffman(&distcode, lengths + nlen, ndist) != 0)
		return -1;

	return Inf_DecompressCodes(s, &lencode, &distcode);
}

// Decompress DEFLATE data. Returns 0 on success, -1 on error.
static int Inf_Inflate(const byte* src, const int src_len, byte* dst, const int dst_len, int* out_len)
{
	inf_state_t s;
	memset(&s, 0, sizeof(s));

	s.in = src;
	s.in_len = src_len;
	s.out = dst;
	s.out_len = dst_len;

	int last;
	do
	{
		last = Inf_GetBits(&s, 1); // Final block flag.
		const int type = Inf_GetBits(&s, 2); // Block type.

		int err;
		switch (type)
		{
			case 0: // Stored (uncompressed) block.
			{
				// Discard remaining bits in current byte.
				s.bitbuf = 0;
				s.bitcnt = 0;

				if (s.in_pos + 4 > s.in_len)
					return -1;

				int len = s.in[s.in_pos] | (s.in[s.in_pos + 1] << 8);
				s.in_pos += 2;

				int nlen = s.in[s.in_pos] | (s.in[s.in_pos + 1] << 8);
				s.in_pos += 2;

				if ((len ^ 0xFFFF) != nlen)
					return -1;

				if (s.in_pos + len > s.in_len || s.out_pos + len > s.out_len)
					return -1;

				memcpy(s.out + s.out_pos, s.in + s.in_pos, len);
				s.in_pos += len;
				s.out_pos += len;
				err = 0;
				break;
			}

			case 1:
				err = Inf_FixedBlock(&s);
				break;

			case 2:
				err = Inf_DynamicBlock(&s);
				break;

			default:
				return -1;
		}

		if (err != 0)
			return -1;

	} while (!last);

	*out_len = s.out_pos;
	return 0;
}

#pragma endregion

#pragma region ========================== ZIP READER ==========================

// ZIP local file header signature.
#define ZIP_LOCAL_SIG		0x04034b50
#define ZIP_CENTRAL_SIG		0x02014b50
#define ZIP_EOCD_SIG		0x06054b50

#define ZIP_METHOD_STORED	0
#define ZIP_METHOD_DEFLATED	8

#pragma pack(push, 1)
typedef struct
{
	unsigned int signature;
	unsigned short version_needed;
	unsigned short flags;
	unsigned short compression;
	unsigned short mod_time;
	unsigned short mod_date;
	unsigned int crc32;
	unsigned int compressed_size;
	unsigned int uncompressed_size;
	unsigned short filename_len;
	unsigned short extra_len;
} zip_local_header_t;
#pragma pack(pop)

// Extract a named file from a ZIP archive to a destination path.
// Returns true on success.
static qboolean ZIP_ExtractFile(const char* zip_path, const char* entry_name, const char* dest_path)
{
	FILE* f;
	if (fopen_s(&f, zip_path, "rb") != 0 || f == NULL)
		return false;

	qboolean result = false;

	// Scan through the ZIP looking for local file headers.
	while (true)
	{
		zip_local_header_t header;
		if (fread(&header, 1, sizeof(header), f) != sizeof(header))
			break;

		if (header.signature != ZIP_LOCAL_SIG)
			break;

		// Read the filename.
		char filename[512];
		const int name_len = min(header.filename_len, (unsigned short)(sizeof(filename) - 1));
		if (fread(filename, 1, header.filename_len, f) != header.filename_len)
			break;

		filename[name_len] = '\0';

		// Skip extra field.
		if (header.extra_len > 0)
			fseek(f, header.extra_len, SEEK_CUR);

		// Check if this is the file we're looking for.
		if (Q_stricmp(filename, entry_name) == 0)
		{
			// Read compressed data.
			byte* comp_data = Z_Malloc(header.compressed_size);
			if (fread(comp_data, 1, header.compressed_size, f) != header.compressed_size)
			{
				Z_Free(comp_data);
				break;
			}

			byte* file_data;
			int file_len;

			if (header.compression == ZIP_METHOD_STORED)
			{
				file_data = comp_data;
				file_len = header.uncompressed_size;
			}
			else if (header.compression == ZIP_METHOD_DEFLATED)
			{
				file_data = Z_Malloc(header.uncompressed_size);

				if (Inf_Inflate(comp_data, header.compressed_size, file_data, header.uncompressed_size, &file_len) != 0
					|| file_len != (int)header.uncompressed_size)
				{
					Com_Printf("ZIP_ExtractFile: inflate failed for '%s'\n", entry_name);
					Z_Free(comp_data);
					Z_Free(file_data);
					break;
				}

				Z_Free(comp_data);
			}
			else
			{
				Com_Printf("ZIP_ExtractFile: unsupported compression method %i for '%s'\n", header.compression, entry_name);
				Z_Free(comp_data);
				break;
			}

			// Write to destination file.
			FILE* out;
			if (fopen_s(&out, dest_path, "wb") == 0 && out != NULL)
			{
				fwrite(file_data, 1, file_len, out);
				fclose(out);
				result = true;
			}
			else
			{
				Com_Printf("ZIP_ExtractFile: failed to write '%s'\n", dest_path);
			}

			Z_Free(file_data);
			break;
		}

		// Skip file data for non-matching entries.
		fseek(f, header.compressed_size, SEEK_CUR);
	}

	fclose(f);
	return result;
}

#pragma endregion

#pragma region ========================== SPLASH WINDOW ==========================

// Resource ID for CDLoad.bmp (must match launcher/resource.h).
#define IDB_CDLOAD 102

static HWND splash_hwnd = NULL;
static HBITMAP splash_bmp = NULL;

static LRESULT CALLBACK SplashWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
	switch (msg)
	{
		case WM_PAINT:
		{
			PAINTSTRUCT ps;
			HDC hdc = BeginPaint(hwnd, &ps);

			if (splash_bmp != NULL)
			{
				BITMAP bm;
				GetObject(splash_bmp, sizeof(bm), &bm);

				HDC mem_dc = CreateCompatibleDC(hdc);
				HGDIOBJ old = SelectObject(mem_dc, splash_bmp);
				BitBlt(hdc, 0, 0, bm.bmWidth, bm.bmHeight, mem_dc, 0, 0, SRCCOPY);
				SelectObject(mem_dc, old);
				DeleteDC(mem_dc);
			}

			EndPaint(hwnd, &ps);
			return 0;
		}

		case WM_ERASEBKGND:
			return 1;

		default:
			return DefWindowProcA(hwnd, msg, wParam, lParam);
	}
}

static void Splash_Show(void)
{
	HINSTANCE exe = GetModuleHandleA(NULL);

	splash_bmp = LoadBitmapA(exe, MAKEINTRESOURCEA(IDB_CDLOAD));
	if (splash_bmp == NULL)
		return;

	BITMAP bm;
	GetObject(splash_bmp, sizeof(bm), &bm);

	// Register window class.
	WNDCLASSA wc;
	memset(&wc, 0, sizeof(wc));
	wc.lpfnWndProc = SplashWndProc;
	wc.hInstance = exe;
	wc.hCursor = LoadCursorA(NULL, IDC_ARROW);
	wc.lpszClassName = "H2R_Splash";
	RegisterClassA(&wc);

	// Center on screen.
	const int sx = GetSystemMetrics(SM_CXSCREEN);
	const int sy = GetSystemMetrics(SM_CYSCREEN);
	const int x = (sx - bm.bmWidth) / 2;
	const int y = (sy - bm.bmHeight) / 2;

	splash_hwnd = CreateWindowExA(WS_EX_TOPMOST, "H2R_Splash", NULL,
		WS_POPUP | WS_VISIBLE, x, y, bm.bmWidth, bm.bmHeight,
		NULL, NULL, exe, NULL);

	if (splash_hwnd != NULL)
	{
		ShowWindow(splash_hwnd, SW_SHOW);
		UpdateWindow(splash_hwnd);

		// Pump messages so the window paints.
		MSG msg;
		while (PeekMessageA(&msg, NULL, 0, 0, PM_REMOVE))
		{
			TranslateMessage(&msg);
			DispatchMessageA(&msg);
		}
	}
}

static void Splash_Hide(void)
{
	if (splash_hwnd != NULL)
	{
		DestroyWindow(splash_hwnd);
		splash_hwnd = NULL;
	}

	UnregisterClassA("H2R_Splash", GetModuleHandleA(NULL));

	if (splash_bmp != NULL)
	{
		DeleteObject(splash_bmp);
		splash_bmp = NULL;
	}
}

#pragma endregion

#pragma region ========================== CD DETECTION ==========================

// Check if a drive contains the Heretic II CD by looking for the installer ZIP.
static qboolean IsHeretic2CD(const char* drive_path)
{
	char zip_path[MAX_OSPATH];
	Com_sprintf(zip_path, sizeof(zip_path), "%sSetup/zip/h2.zip", drive_path);

	// Check if the ZIP file exists.
	FILE* f;
	if (fopen_s(&f, zip_path, "rb") != 0 || f == NULL)
	{
		// Also try backslash separators.
		Com_sprintf(zip_path, sizeof(zip_path), "%sSetup\\zip\\h2.zip", drive_path);
		if (fopen_s(&f, zip_path, "rb") != 0 || f == NULL)
			return false;
	}

	fclose(f);
	return true;
}

const char* CD_GetDrivePath(void)
{
	return cd_detected ? cd_drive_path : NULL;
}

qboolean CD_DetectAndExtract(const char* base_dir)
{
	cd_detected = false;

	// Enumerate all drives and look for CD/DVD drives.
	char drives[256];
	const DWORD len = GetLogicalDriveStringsA(sizeof(drives), drives);
	if (len == 0 || len > sizeof(drives))
		return false;

	for (const char* drv = drives; *drv != '\0'; drv += strlen(drv) + 1)
	{
		if (GetDriveTypeA(drv) != DRIVE_CDROM)
			continue;

		if (!IsHeretic2CD(drv))
			continue;

		// Found the Heretic II CD!
		strcpy_s(cd_drive_path, sizeof(cd_drive_path), drv);
		cd_detected = true;

		Com_Printf("Heretic II CD detected at %s\n", cd_drive_path);

		// Check if PAK files already exist in the base directory.
		char pak0_path[MAX_OSPATH];
		char pak1_path[MAX_OSPATH];
		Com_sprintf(pak0_path, sizeof(pak0_path), "%s/Htic2-0.pak", base_dir);
		Com_sprintf(pak1_path, sizeof(pak1_path), "%s/Htic2-1.pak", base_dir);

		qboolean pak0_exists = false;
		qboolean pak1_exists = false;

		FILE* check;
		if (fopen_s(&check, pak0_path, "rb") == 0 && check != NULL) { fclose(check); pak0_exists = true; }
		if (fopen_s(&check, pak1_path, "rb") == 0 && check != NULL) { fclose(check); pak1_exists = true; }

		if (pak0_exists && pak1_exists)
		{
			Com_Printf("PAK files already present in '%s'.\n", base_dir);
			return true;
		}

		// Build path to the ZIP on the CD.
		char zip_path[MAX_OSPATH];
		Com_sprintf(zip_path, sizeof(zip_path), "%sSetup/zip/h2.zip", cd_drive_path);

		// Try alternate path with backslashes.
		FILE* zf;
		if (fopen_s(&zf, zip_path, "rb") != 0 || zf == NULL)
			Com_sprintf(zip_path, sizeof(zip_path), "%sSetup\\zip\\h2.zip", cd_drive_path);
		else
			fclose(zf);

		// Extract missing PAK files.
		Splash_Show();

		if (!pak0_exists)
		{
			Com_Printf("Extracting Htic2-0.pak from CD...\n");

			if (ZIP_ExtractFile(zip_path, "base/Htic2-0.pak", pak0_path))
				Com_Printf("  Htic2-0.pak extracted successfully.\n");
			else
				Com_Printf("  Failed to extract Htic2-0.pak.\n");
		}

		if (!pak1_exists)
		{
			Com_Printf("Extracting Htic2-1.pak from CD...\n");

			if (ZIP_ExtractFile(zip_path, "base/Htic2-1.pak", pak1_path))
				Com_Printf("  Htic2-1.pak extracted successfully.\n");
			else
				Com_Printf("  Failed to extract Htic2-1.pak.\n");
		}

		Splash_Hide();

		return true;
	}

	return false;
}

#pragma endregion
