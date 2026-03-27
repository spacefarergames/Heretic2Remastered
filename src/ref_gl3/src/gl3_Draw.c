//
// gl3_Draw.c
//
// OpenGL 3.3 2D drawing system using shaders and VBOs.
//

#include "gl3_Draw.h"
#include "gl3_Image.h"
#include "gl3_Model.h"
#include "gl3_Shaders.h"
#include "gl3_Misc.h"
#include "qfiles.h"
#include "Vector.h"
#include "vid.h"

image_t* r_notexture;
image_t* r_particletexture;
image_t* r_aparticletexture;
image_t* r_reflecttexture;
image_t* r_font1;
image_t* r_font2;
image_t* draw_chars;

glxy_t* font1;
glxy_t* font2;

static void InitFonts(void)
{
	ri.FS_LoadFile("pics/misc/font1.fnt", (void**)&font1);
	ri.FS_LoadFile("pics/misc/font2.fnt", (void**)&font2);
}

void ShutdownFonts(void)
{
	if (font1 != NULL)
	{
		ri.FS_FreeFile(font1);
		font1 = NULL;
	}

	if (font2 != NULL)
	{
		ri.FS_FreeFile(font2);
		font2 = NULL;
	}
}

image_t* Draw_FindPic(const char* name)
{
	if (name[0] != '/' && name[0] != '\\')
	{
		char fullname[MAX_QPATH];
		Com_sprintf(fullname, sizeof(fullname), "pics/%s", name);

		return R_FindImage(fullname, it_pic);
	}

	return R_FindImage(name + 1, it_pic);
}

static image_t* Draw_FindPicFilter(const char* name)
{
	if (name[0] != '/' && name[0] != '\\')
	{
		char fullname[MAX_QPATH];
		Com_sprintf(fullname, sizeof(fullname), "pics/%s", name);

		return R_FindImage(fullname, it_sky);
	}

	return R_FindImage(name + 1, it_sky);
}

void Draw_InitLocal(void)
{
	r_notexture = NULL;
	r_notexture = R_FindImage("textures/general/notex.m8", it_wall);
	if (r_notexture == NULL)
		ri.Sys_Error(ERR_DROP, "Draw_InitLocal: could not find textures/general/notex.m8");

	draw_chars = Draw_FindPic("misc/conchars.m32");
	r_particletexture = Draw_FindPicFilter("misc/particle.m32");
	r_aparticletexture = Draw_FindPicFilter("misc/aparticle.m8");
	r_font1 = Draw_FindPic("misc/font1.m32");
	r_font2 = Draw_FindPic("misc/font2.m32");
	r_reflecttexture = Draw_FindPicFilter("misc/reflect.m32");

	InitFonts();
}

// ============================================================
// GL3 2D drawing helpers using VBOs.
// ============================================================

// Draw a textured quad using the 2D shader.
// Vertex layout: x, y, s, t, r, g, b, a (8 floats per vertex).
static void GL3_DrawQuad2D(float x, float y, float w, float h,
	float s0, float t0, float s1, float t1,
	float r, float g, float b, float a)
{
	const float verts[] = {
		x,     y,     s0, t0, r, g, b, a,
		x + w, y,     s1, t0, r, g, b, a,
		x + w, y + h, s1, t1, r, g, b, a,

		x,     y,     s0, t0, r, g, b, a,
		x + w, y + h, s1, t1, r, g, b, a,
		x,     y + h, s0, t1, r, g, b, a,
	};

	GL3_UseShader(gl3state.shader2D);
	glUniform4f(gl3state.uni2D_color, 1.0f, 1.0f, 1.0f, 1.0f);

	glBindVertexArray(gl3state.vao2D);
	glBindBuffer(GL_ARRAY_BUFFER, gl3state.vbo2D);
	glBufferData(GL_ARRAY_BUFFER, sizeof(verts), verts, GL_STREAM_DRAW);
	glDrawArrays(GL_TRIANGLES, 0, 6);
	glBindVertexArray(0);
}

// Draw a colored (untextured) quad for Draw_Fill / Draw_FadeScreen.
static void GL3_DrawColorQuad2D(float x, float y, float w, float h,
	float r, float g, float b, float a)
{
	// Bind a 1x1 white pixel as texture so the shader's texture sample is neutral.
	glBindTexture(GL_TEXTURE_2D, gl3state.whiteTexture);

	const float verts[] = {
		x,     y,     0.0f, 0.0f, r, g, b, a,
		x + w, y,     1.0f, 0.0f, r, g, b, a,
		x + w, y + h, 1.0f, 1.0f, r, g, b, a,

		x,     y,     0.0f, 0.0f, r, g, b, a,
		x + w, y + h, 1.0f, 1.0f, r, g, b, a,
		x,     y + h, 0.0f, 1.0f, r, g, b, a,
	};

	GL3_UseShader(gl3state.shader2D);
	glUniform4f(gl3state.uni2D_color, 1.0f, 1.0f, 1.0f, 1.0f);

	glBindVertexArray(gl3state.vao2D);
	glBindBuffer(GL_ARRAY_BUFFER, gl3state.vbo2D);
	glBufferData(GL_ARRAY_BUFFER, sizeof(verts), verts, GL_STREAM_DRAW);
	glDrawArrays(GL_TRIANGLES, 0, 6);
	glBindVertexArray(0);
}

// ============================================================
// Public drawing functions.
// ============================================================

#define CELL_SIZE	0.0625f // 16 chars per row/column.

static void Draw_Char_impl(const int x, const int y, const int scale, int c, const paletteRGBA_t color)
{
	c &= 255;

	const int char_size = CONCHAR_SIZE * scale;

	if ((c & 127) == 32 || y <= -char_size)
		return;

	const float frow = (float)(c >> 4) * CELL_SIZE;
	const float fcol = (float)(c & 15) * CELL_SIZE;

	R_BindImage(draw_chars);

	glEnable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

	GL3_DrawQuad2D((float)x, (float)y, (float)char_size, (float)char_size,
		fcol, frow, fcol + CELL_SIZE, frow + CELL_SIZE,
		color.r / 255.0f, color.g / 255.0f, color.b / 255.0f, color.a / 255.0f);

	glDisable(GL_BLEND);
}

void Draw_Char(const int x, const int y, const int scale, const int c, const paletteRGBA_t color, const qboolean draw_shadow)
{
	if (draw_shadow)
	{
		const paletteRGBA_t shade_color = { .r = 32, .g = 32, .b = 32, .a = (byte)((float)color.a * 0.75f) };
		Draw_Char_impl(x + scale, y + scale, scale, c, shade_color);
	}

	Draw_Char_impl(x, y, scale, c, color);
}

void Draw_GetPicSize(int* w, int* h, const char* name)
{
	const image_t* image = R_FindImage(name, it_pic);

	if (image != r_notexture)
	{
		*w = image->width;
		*h = image->height;
	}
	else
	{
		*w = 0;
		*h = 0;
	}
}

void Draw_Render(const int x, const int y, const int w, const int h, const image_t* image, const float alpha)
{
	R_BindImage(image);

	glEnable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

	GL3_DrawQuad2D((float)x, (float)y, (float)w, (float)h,
		0.0f, 0.0f, 1.0f, 1.0f,
		1.0f, 1.0f, 1.0f, alpha);

	glDisable(GL_BLEND);
}

void Draw_StretchPic(int x, int y, int w, int h, const char* name, const float alpha, const DrawStretchPicScaleMode_t mode)
{
	const image_t* image = Draw_FindPicFilter(name);

	switch (mode)
	{
		case DSP_SCALE_SCREEN:
		{
			const int xr = x + w;
			const int yb = y + h;

			x = (int)((float)x / DEF_WIDTH * viddef.width);
			y = (int)((float)y / DEF_HEIGHT * viddef.height);
			w = (int)((float)xr / DEF_WIDTH * viddef.width) - x;
			h = (int)((float)yb / DEF_HEIGHT * viddef.height) - y;
		}
		break;

		case DSP_SCALE_4x3:
		{
			const int full_w = viddef.width;
			const int full_h = viddef.height;
			const int target_w = (full_h * 4) / 3;
			const int offset_x = (full_w - target_w) / 2;

			const int xr = x + w;
			const int yb = y + h;

			x = offset_x + (int)((float)x / DEF_WIDTH * target_w);
			y = (int)((float)y / DEF_HEIGHT * full_h);
			w = offset_x + (int)((float)xr / DEF_WIDTH * target_w) - x;
			h = (int)((float)yb / DEF_HEIGHT * full_h) - y;
		}
		break;

		default:
			break;
	}

	Draw_Render(x, y, w, h, image, alpha);
}

void Draw_Pic(const int x, const int y, const int scale, const char* name, const float alpha)
{
	const image_t* image = Draw_FindPic(name);

	if (image != NULL)
		Draw_Render(x, y, image->width * scale, image->height * scale, image, alpha);
}

void Draw_TileClear(const int x, const int y, const int w, const int h, const char* pic)
{
	const image_t* image = Draw_FindPic(pic);

	if (image == NULL)
		return;

	R_BindImage(image);

	// Tile the texture across the area.
	const float s1 = (float)x / (float)image->width;
	const float t1 = (float)y / (float)image->height;
	const float s2 = (float)(x + w) / (float)image->width;
	const float t2 = (float)(y + h) / (float)image->height;

	GL3_DrawQuad2D((float)x, (float)y, (float)w, (float)h,
		s1, t1, s2, t2,
		1.0f, 1.0f, 1.0f, 1.0f);
}

void Draw_Fill(const int x, const int y, const int w, const int h, const paletteRGBA_t color)
{
	glEnable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

	GL3_DrawColorQuad2D((float)x, (float)y, (float)w, (float)h,
		color.r / 255.0f, color.g / 255.0f, color.b / 255.0f, color.a / 255.0f);

	glDisable(GL_BLEND);
}

void Draw_FadeScreen(const paletteRGBA_t color)
{
	glEnable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

	GL3_DrawColorQuad2D(0.0f, 0.0f, (float)viddef.width, (float)viddef.height,
		color.r / 255.0f, color.g / 255.0f, color.b / 255.0f, color.a / 255.0f);

	glDisable(GL_BLEND);
}

//mxd. Not part of original logic.
static const glxy_t* GetCharDef(const byte c, const glxy_t* font)
{
	const glxy_t* char_def = &font[c - 32];
	return ((char_def->w == 0) ? &font[14] : char_def); // Return dot char when w == 0?
}

//mxd. Not part of original logic.
static void DrawBigFontChar(const int x, int y, const int offset_x, const int offset_y, const int width, const int height, const float alpha, const glxy_t* char_def)
{
	y -= char_def->baseline;

	const int xl = (width * x / DEF_WIDTH) + offset_x;
	const int xr = (width * (char_def->w + x) / DEF_WIDTH) + offset_x;
	const int yt = (height * y / DEF_HEIGHT) + offset_y;
	const int yb = (height * (char_def->h + y) / DEF_HEIGHT) + offset_y;

	GL3_DrawQuad2D((float)xl, (float)yt, (float)(xr - xl), (float)(yb - yt),
		char_def->xl, char_def->yt, char_def->xr, char_def->yb,
		1.0f, 1.0f, 1.0f, alpha);
}

void Draw_BigFont(const int x, const int y, const char* text, const float alpha)
{
	if (font1 == NULL || font2 == NULL)
		return;

	glEnable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

	const glxy_t* cur_font = font1;
	R_BindImage(r_font1);

	int ox = x;
	int oy = y;

	int vid_w = viddef.width;
	int vid_h = viddef.height;

	int offset_x = 0;
	int offset_y = 0;

	if ((float)vid_w * 0.75f > (float)vid_h) //mxd. Setup for widescreen aspect ratio.
	{
		vid_w = vid_h * 4 / 3;
		offset_x = (viddef.width - vid_w) / 2;
	}

	while (true)
	{
		const byte c = *text++;

		if (c == 0)
			break;

		switch (c)
		{
			case 1:
				ox = (DEF_WIDTH - BF_Strlen(text)) / 2;
				break;

			case 2:
				cur_font = font1;
				R_BindImage(r_font1);
				break;
			case 3:
				cur_font = font2;
				R_BindImage(r_font2);
				break;

			case '\t':
				ox += 63;
				break;

			case '\n':
				oy += 18;
				ox = x;
				break;

			case '\r':
				break;

			case 32: // Whitespace char.
				ox += 8;
				break;

			default:
				if (c > 32)
				{
					const glxy_t* char_def = GetCharDef(c, cur_font);
					DrawBigFontChar(ox, oy, offset_x, offset_y, vid_w, vid_h, alpha, char_def);
					ox += char_def->w;
				}
				break;
		}
	}

	glDisable(GL_BLEND);
}

// BigFont_Strlen. Returns width of given text in pixels.
int BF_Strlen(const char* text)
{
	if (font1 == NULL || font2 == NULL)
		return 0;

	int width = 0;
	const glxy_t* cur_font = font1;

	while (true)
	{
		const byte c = *text++;

		switch (c)
		{
			case 0:
			case 1:
			case '\t':
			case '\n':
				return width;

			case 2:
				cur_font = font1;
				break;

			case 3:
				cur_font = font2;
				break;

			case '\r':
				break;

			case 32: // Whitespace char.
				width += 8;
				break;

			default:
				if (c > 32) // When printable char.
				{
					const glxy_t* char_def = GetCharDef(c, cur_font);
					width += char_def->w;
				}
				break;
		}
	}
}

void Draw_BookPic(const char* name, const float scale, const float alpha)
{
	const model_t* mod = RI_RegisterModel(name);

	if (mod == NULL)
	{
		ri.Con_Printf(PRINT_ALL, "Draw_BookPic: can't find book '%s'\n", name);
		return;
	}

	book_t* book = mod->extradata;

	const float header_w = (float)book->bheader.total_w;
	const float header_h = (float)book->bheader.total_h;

	float vid_w = (float)viddef.width;
	float vid_h = (float)viddef.height;

	int offset_x = 0;
	int offset_y = 0;

	if (vid_w * 0.75f > vid_h) //mxd. Setup for widescreen aspect ratio.
	{
		vid_w = vid_h * 4 / 3;
		offset_x = (viddef.width - (int)vid_w) / 2;
	}

	offset_x += (int)((header_w - header_w * scale) * 0.5f * (vid_w / header_w));
	offset_y += (int)((header_h - header_h * scale) * 0.5f * (vid_h / header_h));

	bookframe_t* bframe = &book->bframes[0];
	for (int i = 0; i < book->bheader.num_segments; i++, bframe++)
	{
		const int pic_x = (int)floorf((float)bframe->x * vid_w / header_w * scale);
		const int pic_y = (int)floorf((float)bframe->y * vid_h / header_h * scale);
		const int pic_w = (int)ceilf((float)bframe->w * vid_w / header_w * scale);
		const int pic_h = (int)ceilf((float)bframe->h * vid_h / header_h * scale);

		Draw_Render(offset_x + pic_x, offset_y + pic_y, pic_w, pic_h, mod->skins[i], gl_bookalpha->value * alpha);
	}
}

void Draw_Name(const vec3_t origin, const char* name, const paletteRGBA_t color)
{
	// Stub - requires 3D-to-screen projection which needs the full 3D pipeline.
}

// ============================================================
// Cinematic rendering (GL3).
//
// palette == NULL  ->  data is BGRA from WMF (MFVideoFormat_RGB32, bottom-up).
//                      Uploaded with GL_BGRA; drawn with V-flipped UVs so the
//                      image is displayed right-side up on screen.
// palette != NULL  ->  data is 8-bit palettized (SMK).  Each byte is a palette
//                      index; the 256-entry RGB palette is in 'palette'.
// ============================================================

static GLuint cin_texture  = 0;
static int    cin_width    = 0;
static int    cin_height   = 0;
static byte*  cin_rgba     = NULL; // Expand buffer for palettized (SMK) path.
static GLuint cin_pbo[2]   = { 0, 0 }; // Double-buffered PBOs for async MP4 upload.
static int    cin_pbo_idx  = 0;         // Index of the PBO being written by the CPU this frame.

void Draw_InitCinematic(const int width, const int height)
{
	// Clean up any previous cinematic texture.
	Draw_CloseCinematic();

	cin_width  = width;
	cin_height = height;

	glGenTextures(1, &cin_texture);
	glBindTexture(GL_TEXTURE_2D, cin_texture);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	// WMF (MFVideoFormat_RGB32 / BGRA) outputs alpha=0 on many drivers.
	// Force the sampled alpha to always return 1.0 so the shader's
	// discard-on-alpha test never falsely rejects video pixels.
	const GLint swizzle_a = GL_ONE;
	glTexParameteriv(GL_TEXTURE_2D, GL_TEXTURE_SWIZZLE_A, &swizzle_a);
	glBindTexture(GL_TEXTURE_2D, 0);

	cin_rgba = malloc((size_t)(width * height * 4));

	// Allocate two PBOs for double-buffered async uploads (MP4 path).
	// Pre-fill with zeroes so the first async upload shows black rather than garbage.
	const GLsizeiptr pbo_size = (GLsizeiptr)(width * height * 4);
	glGenBuffers(2, cin_pbo);
	for (int i = 0; i < 2; i++)
	{
		glBindBuffer(GL_PIXEL_UNPACK_BUFFER, cin_pbo[i]);
		glBufferData(GL_PIXEL_UNPACK_BUFFER, pbo_size, NULL, GL_STREAM_DRAW);
	}
	glBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);
	cin_pbo_idx = 0;
}

void Draw_CloseCinematic(void)
{
	if (cin_texture != 0)
	{
		glDeleteTextures(1, &cin_texture);
		cin_texture = 0;
	}
	if (cin_rgba != NULL)
	{
		free(cin_rgba);
		cin_rgba = NULL;
	}
	if (cin_pbo[0] != 0)
	{
		glBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);
		glDeleteBuffers(2, cin_pbo);
		cin_pbo[0] = cin_pbo[1] = 0;
	}
	cin_pbo_idx = 0;
	cin_width  = 0;
	cin_height = 0;
}

void Draw_Cinematic(const byte* data, const paletteRGB_t* palette)
{
	if (cin_texture == 0 || data == NULL)
		return;

	// Clear the back buffer to black each cinematic frame.
	// R_Clear only clears depth; without this the two double-buffered
	// back buffers show stale content (loading screen / 3D scene) on
	// frames where video pixels are discarded, producing a visible flicker.
	glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
	glClear(GL_COLOR_BUFFER_BIT);

	// Ensure the 2D projection covers the full screen in pixel coords.
	GL3_UpdateProjection2D((float)viddef.width, (float)viddef.height);

	// Upload the video frame to the cinematic texture.
	// Explicitly select TMU0: post-processing leaves the active unit at TMU2 (AO),
	// which would misdirect both the upload and the draw if not reset here.
	glActiveTexture(GL_TEXTURE0);
	glBindTexture(GL_TEXTURE_2D, cin_texture);

	if (palette == NULL)
	{
		// MP4 path: double-buffered PBO async upload.
		// Frame N: GPU DMAs from pbo[upload_idx] to the texture (no CPU stall).
		//          CPU writes the new frame into pbo[write_idx] via map/unmap.
		// The 1-frame latency (texture lags data by one frame) is imperceptible at video FPS.
		const GLsizeiptr pbo_size  = (GLsizeiptr)(cin_width * cin_height * 4);
		const int        write_idx  = cin_pbo_idx;
		const int        upload_idx = 1 - cin_pbo_idx;
		cin_pbo_idx = upload_idx; // Swap for next frame.

		// Async DMA: PBO[upload_idx] → texture.  NULL offset means "read from bound PBO".
		glBindBuffer(GL_PIXEL_UNPACK_BUFFER, cin_pbo[upload_idx]);
		glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, cin_width, cin_height, GL_BGRA, GL_UNSIGNED_BYTE, NULL);

		// CPU fills PBO[write_idx] with the current frame.
		// GL_MAP_INVALIDATE_BUFFER_BIT lets the driver give us fresh memory without
		// waiting for any pending GPU read of the old contents.
		glBindBuffer(GL_PIXEL_UNPACK_BUFFER, cin_pbo[write_idx]);
		void* ptr = glMapBufferRange(GL_PIXEL_UNPACK_BUFFER, 0, pbo_size,
			GL_MAP_WRITE_BIT | GL_MAP_INVALIDATE_BUFFER_BIT);
		if (ptr != NULL)
		{
			memcpy(ptr, data, (size_t)pbo_size);
			glUnmapBuffer(GL_PIXEL_UNPACK_BUFFER);
		}
		glBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);
	}
	else
	{
		// SMK path: expand 8-bit palettized data to RGBA.
		if (cin_rgba != NULL)
		{
			const int npixels = cin_width * cin_height;
			for (int i = 0; i < npixels; i++)
			{
				const byte idx = data[i];
				cin_rgba[i * 4 + 0] = palette[idx].r;
				cin_rgba[i * 4 + 1] = palette[idx].g;
				cin_rgba[i * 4 + 2] = palette[idx].b;
				cin_rgba[i * 4 + 3] = 255;
			}
			glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, cin_width, cin_height,
				GL_RGBA, GL_UNSIGNED_BYTE, cin_rgba);
		}
	}

	// Compute letterboxed destination rect (preserve aspect ratio).
	const float scr_w = (float)viddef.width;
	const float scr_h = (float)viddef.height;
	const float scale = min(scr_w / (float)cin_width, scr_h / (float)cin_height);
	const float dw    = (float)cin_width  * scale;
	const float dh    = (float)cin_height * scale;
	const float dx    = (scr_w - dw) * 0.5f;
	const float dy    = (scr_h - dh) * 0.5f;

	// Black letterbox bars.
	const paletteRGBA_t black = { 0, 0, 0, 255 };
	if (dx > 0.5f)
	{
		Draw_Fill(0, 0, (int)dx, viddef.height, black);
		Draw_Fill((int)(dx + dw), 0, viddef.width - (int)(dx + dw), viddef.height, black);
	}
	if (dy > 0.5f)
	{
		Draw_Fill(0, 0, viddef.width, (int)dy, black);
		Draw_Fill(0, (int)(dy + dh), viddef.width, viddef.height - (int)(dy + dh), black);
	}

	// Draw the cinematic quad.
	// GL3_DrawQuad2D(x, y, w, h, s0, t0, s1, t1, r, g, b, a)
	// WMF MFVideoFormat_RGB32 with MF_SOURCE_READER_ENABLE_VIDEO_PROCESSING outputs
	// top-down frames, so no V-flip is needed — use normal (0 → 1) texture coordinates.
	const float t0 = 0.0f;
	const float t1 = 1.0f;

	GL3_UseShader(gl3state.shader2D);
	glUniform4f(gl3state.uni2D_color, 1.0f, 1.0f, 1.0f, 1.0f);
	glBindTexture(GL_TEXTURE_2D, cin_texture);
	GL3_DrawQuad2D(dx, dy, dw, dh, 0.0f, t0, 1.0f, t1, 1.0f, 1.0f, 1.0f, 1.0f);
}
