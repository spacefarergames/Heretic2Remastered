//
// glimp_sdl3.c --	This is the client side of the render backend, implemented trough SDL.
//					The SDL window and related functions (mouse grab, fullscreen switch) are implemented here,
//					everything else is in the renderers.
//
// Copyright 2025 mxd
//

#include "glimp_sdl3.h"
#include "qcommon.h"
#include "client.h"
#include <SDL3/SDL.h>

static int last_flags = 0;
static SDL_Window* window = NULL;
static cvar_t* vid_hidpi;

static qboolean CreateSDLWindow(const SDL_WindowFlags flags, const int width, const int height)
{
	// Force the window to minimize when focus is lost. 
	// The windows staying maximized has some odd implications for window ordering under Windows and some X11 window managers like kwin.
	// See: https://github.com/libsdl-org/SDL/issues/4039 https://github.com/libsdl-org/SDL/issues/3656
	SDL_SetHint(SDL_HINT_VIDEO_MINIMIZE_ON_FOCUS_LOSS, "1");

	const SDL_PropertiesID props = SDL_CreateProperties();

	SDL_SetStringProperty(props, SDL_PROP_WINDOW_CREATE_TITLE_STRING, GAME_NAME);
	SDL_SetNumberProperty(props, SDL_PROP_WINDOW_CREATE_X_NUMBER, SDL_WINDOWPOS_CENTERED);
	SDL_SetNumberProperty(props, SDL_PROP_WINDOW_CREATE_Y_NUMBER, SDL_WINDOWPOS_CENTERED);
	SDL_SetNumberProperty(props, SDL_PROP_WINDOW_CREATE_WIDTH_NUMBER, width);
	SDL_SetNumberProperty(props, SDL_PROP_WINDOW_CREATE_HEIGHT_NUMBER, height);
	SDL_SetNumberProperty(props, SDL_PROP_WINDOW_CREATE_FLAGS_NUMBER, (Sint64)flags);

	window = SDL_CreateWindowWithProperties(props);
	SDL_DestroyProperties(props);

	if (window != NULL)
	{
		// Enable text input.
		SDL_StartTextInput(window);
		return true;
	}

	Com_Printf("ERROR: Creating SDL window failed: %s\n", SDL_GetError());
	Com_Printf("Window parameters: %ix%i, flags: 0x%x\n", width, height, flags);
	return false;
}

static qboolean InitDisplayModes(void) //mxd
{
	uint cur_display;

	if (window == NULL)
	{
		// Called without a window, list modes from the first display.
		// This is the primary display and likely the one the game will run on.
		cur_display = SDL_GetPrimaryDisplay();
	}
	else
	{
		// Otherwise use the display were the window is displayed.
		// There are some obscure setups were this can fail (one X11 server with several screen is one of these), so add a fallback to the first display.
		cur_display = SDL_GetDisplayForWindow(window);
		if (cur_display == 0)
			cur_display = SDL_GetPrimaryDisplay();
	}

	int num_modes = 0;
	SDL_DisplayMode** modes = SDL_GetFullscreenDisplayModes(cur_display, &num_modes);

	if (modes != NULL)
	{
		SDL_Rect safe_bounds;
		if (!SDL_GetDisplayUsableBounds(cur_display, &safe_bounds))
		{
			Com_Printf("Failed get usable display bounds: %s\n", SDL_GetError());
			return false;
		}

		//mxd. Collect available display resolutions, in descending order, starting at desktop resolution and stopping at 640x480.
		// Good thing:	all display modes are guaranteed to be available on this display.
		// Bad thing:	same vid_mode cvar value will result in different resolutions on different PCs / displays...
		//TODO: mode 0 needs to be re-initialized when switching target display.
		viddef_t* valid_modes = malloc(sizeof(viddef_t) * num_modes);

		// Mode 0 is desktop resolution.
		const SDL_DisplayMode* desktop_mode = SDL_GetDesktopDisplayMode(cur_display);
		valid_modes->width = desktop_mode->w;
		valid_modes->height = desktop_mode->h;
		int num_valid_modes = 1;

		// Add resolutions for windowed modes.
		for (int i = 1; i < num_modes; i++)
		{
			const SDL_DisplayMode* mode = modes[i];

			if (mode->w > safe_bounds.w || mode->h > safe_bounds.h || mode->displayID != cur_display)
				continue;

			if (mode->w < DEF_WIDTH && mode->h < DEF_HEIGHT)
				break;

			// Check if already added. Modes can differ by pixel format, pixel density or refresh rate only...
			qboolean skip_mode = false;
			for (int c = 0; c < num_valid_modes; c++)
			{
				if (valid_modes[c].width == mode->w && valid_modes[c].height == mode->h)
				{
					skip_mode = true; // Already added...
					break;
				}
			}

			if (!skip_mode)
			{
				valid_modes[num_valid_modes].width = mode->w;
				valid_modes[num_valid_modes].height = mode->h;

				num_valid_modes++;
			}
		}

		SDL_free(modes);

		// Need at least 2 modes, one for fullscreen, one for windowed mode...
		if (num_valid_modes < 2)
		{
			Com_Printf("Failed to initialize display modes!\n");
			free(valid_modes);

			return false;
		}

		VID_InitModes(valid_modes, num_valid_modes); // Store in SDL-independent fashion...

		// List detected modes.
		Com_DPrintf("SDL display modes:\n");
		for (int i = 0; i < num_valid_modes; i++)
			Com_DPrintf(" - Mode %2i: %ix%i\n", i, valid_modes[i].width, valid_modes[i].height);

		free(valid_modes);

		return true;
	}

	Com_Printf("Couldn't get display modes: %s\n", SDL_GetError());
	return false;
}

// Sets the window icon.
static void SetSDLIcon(void)
{
	//TODO: implement? Game window already uses correct icon... somehow.
}

// Shuts the SDL render backend down.
static void ShutdownGraphics(void)
{
	if (window != NULL)
	{
		GLimp_GrabInput(false); // Cleanly ungrab input (needs window).
		SDL_DestroyWindow(window);

		window = NULL;
	}
}

// Initializes the SDL video subsystem. Must be called before anything else.
qboolean GLimp_Init(void)
{
	if (!SDL_WasInit(SDL_INIT_VIDEO))
	{
		if (!SDL_Init(SDL_INIT_VIDEO))
		{
			Com_Printf("Couldn't init SDL video: %s.\n", SDL_GetError());
			return false;
		}

		const int version = SDL_GetVersion();

		Com_Printf("-------- vid initialization --------\n");

		Com_Printf("SDL version is: %i.%i.%i\n", SDL_VERSIONNUM_MAJOR(version), SDL_VERSIONNUM_MINOR(version), SDL_VERSIONNUM_MICRO(version));
		Com_Printf("SDL video driver is \"%s\".\n", SDL_GetCurrentVideoDriver());

		vid_hidpi = Cvar_Get("vid_hidpi", "0", CVAR_ARCHIVE);

		if (!InitDisplayModes()) //mxd
			return false;

		Com_Printf("------------------------------------\n\n");
	}

	return true;
}

// Shuts the SDL video subsystem down. Must be called after everything's finished and cleaned up.
void GLimp_Shutdown(void)
{
	ShutdownGraphics();
	SDL_QuitSubSystem(SDL_INIT_VIDEO);
}

// (Re)initializes the actual window.
qboolean GLimp_InitGraphics(const int width, const int height)
{
	// Is the surface used?
	if (window != NULL) //TODO: can't we just resize it?..
	{
		re.ShutdownContext();
		ShutdownGraphics();

		window = NULL;
	}

	if (last_flags != -1 && (last_flags & SDL_WINDOW_OPENGL))
		SDL_GL_ResetAttributes(); // Reset SDL.

	// Let renderer prepare things (set OpenGL attributes).
	// FIXME: This is no longer necessary, the renderer could and should pass the flags when calling this function.
	SDL_WindowFlags flags = re.PrepareForWindow();

	if ((int)flags == -1)
		return false; // It's PrepareForWindow() job to log an error.

	// Enable HiDPI / high pixel density rendering when requested.
	if ((int)vid_hidpi->value)
		flags |= SDL_WINDOW_HIGH_PIXEL_DENSITY;

	// Create the window. Will be borderless if width and height match current screen resolution.
	// If this fails, R_SetMode() will retry with gl_state.prev_mode.
	if (!CreateSDLWindow(flags, width, height))
	{
		// If window creation failed and MSAA was requested, try again without it.
		// Some systems/drivers (especially under Wine) don't support certain MSAA configurations.
		if (Cvar_VariableInt("r_antialiasing") == 1)
		{
			Com_Printf("MSAA window creation failed, retrying without MSAA...\n");
			SDL_GL_ResetAttributes();

			// Disable MSAA and try again
			SDL_GL_SetAttribute(SDL_GL_MULTISAMPLEBUFFERS, 0);
			SDL_GL_SetAttribute(SDL_GL_MULTISAMPLESAMPLES, 0);

			// Re-request other attributes
			flags = re.PrepareForWindow();
			if ((int)flags == -1)
				return false;

			if ((int)vid_hidpi->value)
				flags |= SDL_WINDOW_HIGH_PIXEL_DENSITY;

			// Try window creation again
			if (!CreateSDLWindow(flags, width, height))
			{
				Com_Printf("Window creation failed even without MSAA\n");
				return false;
			}

			Com_Printf("Window created successfully without MSAA\n");
		}
		else
		{
			return false;
		}
	}

	last_flags = (int)flags;

	// Initialize rendering context.
	if (!re.InitContext(window))
		return false; // InitContext() should have logged an error.

	// When HiDPI is enabled, use the actual drawable pixel size for rendering.
	// On HiDPI displays this will be larger than the window size in screen coordinates.
	if ((int)vid_hidpi->value)
	{
		int draw_w = 0, draw_h = 0;
		if (SDL_GetWindowSizeInPixels(window, &draw_w, &draw_h))
		{
			viddef.width = draw_w;
			viddef.height = draw_h;
			Com_Printf("HiDPI: window %ix%i, drawable %ix%i (scale %.1fx)\n",
				width, height, draw_w, draw_h, (float)draw_w / (float)width);
		}
		else
		{
			viddef.width = width;
			viddef.height = height;
		}
	}
	else
	{
		viddef.width = width;
		viddef.height = height;
	}

	SetSDLIcon();
	SDL_ShowCursor();

	return true;
}

// Shuts the window down.
void GLimp_ShutdownGraphics(void)
{
	SDL_GL_ResetAttributes();
	ShutdownGraphics();
}

// (Un)grab Input.
void GLimp_GrabInput(const qboolean grab)
{
	if (window != NULL)
	{
		if (!SDL_SetWindowMouseGrab(window, grab))
			Com_Printf("WARNING: failed to lock mouse to game window, reason: %s\n", SDL_GetError());

		if (!SDL_SetWindowRelativeMouseMode(window, grab))
			Com_Printf("WARNING: failed to set relative mouse mode, reason: %s\n", SDL_GetError());
	}
}