//
// gl3_SDL.c
//
// OpenGL 3.3 Core Profile SDL context management.
//

#include "gl3_SDL.h"
#include "gl3_Local.h"
#include <SDL3/SDL.h>

static SDL_Window* window = NULL;
static SDL_GLContext context = NULL;

// Swaps the buffers and shows the next frame.
void RI_EndFrame(void)
{
	SDL_GL_SwapWindow(window);
}

// Returns the flags used at the SDL window creation.
// In case of error -1 is returned.
int RI_PrepareForWindow(void)
{
	// Request OpenGL 3.3 Core Profile.
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);

#ifdef _DEBUG
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, SDL_GL_CONTEXT_DEBUG_FLAG);
#endif

	// Set GL context attributes bound to the window.
	SDL_GL_SetAttribute(SDL_GL_RED_SIZE, 8);
	SDL_GL_SetAttribute(SDL_GL_GREEN_SIZE, 8);
	SDL_GL_SetAttribute(SDL_GL_BLUE_SIZE, 8);
	SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
	SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
	SDL_GL_SetAttribute(SDL_GL_ACCELERATED_VISUAL, 1);

	SDL_GL_SetAttribute(SDL_GL_MULTISAMPLEBUFFERS, 0);
	SDL_GL_SetAttribute(SDL_GL_MULTISAMPLESAMPLES, 0);

	return SDL_WINDOW_OPENGL;
}

// Enables or disables the vsync.
void R_SetVsync(void)
{
	int vsync = 0;

	if (r_vsync->value == 1.0f)
		vsync = 1;
	else if (r_vsync->value == 2.0f)
		vsync = -1;

	if (!SDL_GL_SetSwapInterval(vsync) && vsync == -1)
	{
		ri.Con_Printf(PRINT_ALL, "Failed to set adaptive VSync, reverting to normal VSync.\n");
		SDL_GL_SetSwapInterval(1);
	}

	if (!SDL_GL_GetSwapInterval(&vsync))
		ri.Con_Printf(PRINT_ALL, "Failed to get VSync state, assuming no VSync.\n");
}

// Initializes the OpenGL 3.3 Core context.
qboolean RI_InitContext(void* win)
{
	if (win == NULL)
	{
		ri.Sys_Error(ERR_FATAL, "RI_InitContext() called with NULL argument!");
		return false;
	}

	window = (SDL_Window*)win;

	// Initialize GL context.
	context = SDL_GL_CreateContext(window);

	if (context == NULL)
	{
		ri.Con_Printf(PRINT_ALL, "RI_InitContext(): failed to create OpenGL 3.3 context: %s\n", SDL_GetError());
		window = NULL;

		return false;
	}

	// Load OpenGL function pointers through GLAD.
	if (!gladLoadGLLoader((GLADloadproc)SDL_GL_GetProcAddress))
	{
		ri.Con_Printf(PRINT_ALL, "RI_InitContext(): failed to initialize OpenGL via GLAD\n");
		return false;
	}

	// Check OpenGL version.
	if (!GLAD_GL_VERSION_3_3)
	{
		ri.Con_Printf(PRINT_ALL, "RI_InitContext(): unsupported OpenGL version. Expected 3.3, got %i.%i!\n", GLVersion.major, GLVersion.minor);
		return false;
	}

	R_SetVsync();
	vid_gamma->modified = true; // Force R_UpdateGamma() call in R_BeginFrame().

	return true;
}

// Shuts the GL context down.
void RI_ShutdownContext(void)
{
	if (window != NULL && context != NULL)
	{
		SDL_GL_DestroyContext(context);
		context = NULL;
	}
}
