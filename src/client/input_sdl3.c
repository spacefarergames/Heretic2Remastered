//
// input_sdl3.c
//
// Copyright 1998 Raven Software
//

#include "input.h"
#include "Angles.h"
#include "client.h"
#include "glimp_sdl3.h"
#include "qcommon.h"
#include "vid_dll.h"

#include <SDL3/SDL.h>

uint sys_frame_time; //TODO: replace with curtime?

// Mouse vars:
static float mouse_x;
static float mouse_y;
static cvar_t* m_filter;
static qboolean mlooking;

void IN_GetClipboardText(char* out, const size_t n) // YQ2
{
	char* s = SDL_GetClipboardText();

	if (s == NULL || *s == '\0')
	{
		*out = '\0';
		return;
	}

	strcpy_s(out, n - 1, s);
	SDL_free(s);
}

static qboolean IN_ShouldGrabInput(void) //mxd
{
	if (vid_mode->value == 0.0f) // Always hide mouse when in fullscreen mode.
		return true;

	if (cls.key_dest == key_menu) // Show mouse when in menus.
		return false;

	if (cls.key_dest == key_console) // Show mouse when in console.
		return false;

	if (cls.key_dest == key_game && (cls.state == ca_disconnected || cls.state == ca_connecting)) // Show mouse when in fullscreen console.
		return false;

	return true; // Otherwise hide mouse.
}

// This function translates SDL keycodes into the id Tech 2 engines internal representation.
static int IN_TranslateSDLtoQ2Key(const uint keysym)
{
	// These must be translated.
	switch (keysym)
	{
		case SDLK_TAB:			return K_TAB;
		case SDLK_RETURN:		return K_ENTER;
		case SDLK_ESCAPE:		return K_ESCAPE;
		case SDLK_BACKSPACE:	return K_BACKSPACE;
		case SDLK_CAPSLOCK:		return K_CAPSLOCK;
		case SDLK_PAUSE:		return K_PAUSE;

		case SDLK_UP:			return K_UPARROW;
		case SDLK_DOWN:			return K_DOWNARROW;
		case SDLK_LEFT:			return K_LEFTARROW;
		case SDLK_RIGHT:		return K_RIGHTARROW;

		case SDLK_RALT:			return K_ALT;
		case SDLK_LALT:			return K_ALT;

		case SDLK_LCTRL:		return K_CTRL;
		case SDLK_RCTRL:		return K_CTRL;

		case SDLK_LSHIFT:		return K_SHIFT;
		case SDLK_RSHIFT:		return K_SHIFT;

		case SDLK_INSERT:		return K_INS;
		case SDLK_DELETE:		return K_DEL;
		case SDLK_PAGEDOWN:		return K_PGDN;
		case SDLK_PAGEUP:		return K_PGUP;
		case SDLK_HOME:			return K_HOME;
		case SDLK_END:			return K_END;

		case SDLK_F1:			return K_F1;
		case SDLK_F2:			return K_F2;
		case SDLK_F3:			return K_F3;
		case SDLK_F4:			return K_F4;
		case SDLK_F5:			return K_F5;
		case SDLK_F6:			return K_F6;
		case SDLK_F7:			return K_F7;
		case SDLK_F8:			return K_F8;
		case SDLK_F9:			return K_F9;
		case SDLK_F10:			return K_F10;
		case SDLK_F11:			return K_F11;
		case SDLK_F12:			return K_F12;

		case SDLK_KP_0:			return K_KP_INS;
		case SDLK_KP_1:			return K_KP_END;
		case SDLK_KP_2:			return K_KP_DOWNARROW;
		case SDLK_KP_3:			return K_KP_PGDN;
		case SDLK_KP_4:			return K_KP_LEFTARROW;
		case SDLK_KP_5:			return K_KP_5;
		case SDLK_KP_6:			return K_KP_RIGHTARROW;
		case SDLK_KP_7:			return K_KP_HOME;
		case SDLK_KP_8:			return K_KP_UPARROW;
		case SDLK_KP_9:			return K_KP_PGUP;
		case SDLK_KP_ENTER:		return K_KP_ENTER;
		case SDLK_KP_PERIOD:	return K_KP_DEL;
		case SDLK_KP_DIVIDE:	return K_KP_SLASH;
		case SDLK_KP_MINUS:		return K_KP_MINUS;
		case SDLK_KP_PLUS:		return K_KP_PLUS;
		case SDLK_NUMLOCKCLEAR:	return K_KP_NUMLOCK;

		default:				return 0;
	}
}

#pragma region ========================== MOUSE CONTROL ==========================

// Q2 counterpart
static void IN_MLookDown(void) //TODO: ancient "mouselook only when "mlook" key is pressed" logic. Remove?
{
	mlooking = true;
}

// Q2 counterpart
static void IN_MLookUp(void) //TODO: ancient "mouselook only when "mlook" key is pressed" logic. Remove?
{
	mlooking = false;

	if (!(int)freelook->value && (int)lookspring->value)
		IN_CenterView();
}

static void IN_InitMouse(void)
{
	mouse_x = 0.0f;
	mouse_y = 0.0f;

	m_filter = Cvar_Get("m_filter", "0", 0);

	Cmd_AddCommand("+mlook", IN_MLookDown);
	Cmd_AddCommand("-mlook", IN_MLookUp);
}

static void IN_ShutdownMouse(void)
{
	Cmd_RemoveCommand("+mlook");
	Cmd_RemoveCommand("-mlook");
}

static void IN_MouseMove(usercmd_t* cmd)
{
	static float old_mouse_x;
	static float old_mouse_y;

	if (CL_IgnoreInput()) //mxd. Skip when looking through remote camera.
		return;

	if ((int)m_filter->value)
	{
		mouse_x = (mouse_x + old_mouse_x) * 0.5f;
		mouse_y = (mouse_y + old_mouse_y) * 0.5f;
	}

	old_mouse_x = mouse_x;
	old_mouse_y = mouse_y;

	if (mouse_x == 0.0f && mouse_y == 0.0f)
		return;

	mouse_x *= mouse_sensitivity_x->value; // 'sensitivity' cvar in Q2
	mouse_y *= mouse_sensitivity_y->value; // 'sensitivity' cvar in Q2

	// Add mouse X/Y movement to cmd.
	if ((in_strafe.state & KS_DOWN) || ((int)lookstrafe->value && mlooking)) //TODO: remove 'lookstrafe' cvar, always freelook.
		cmd->sidemove += (short)(mouse_x * m_side->value);
	else
		cl.delta_inputangles[YAW] -= mouse_x * m_yaw->value;

	if (!(in_strafe.state & KS_DOWN) || ((int)freelook->value && mlooking)) // H2: no 'else' case //TODO: remove freelook cvar, always freelook.
		cl.delta_inputangles[PITCH] += mouse_y * m_pitch->value;

	// Reset mouse position.
	mouse_x = 0.0f;
	mouse_y = 0.0f;
}

#pragma endregion

#pragma region ========================== GAMEPAD CONTROL ==========================

static SDL_Gamepad* controller = NULL;
static SDL_JoystickID controller_id = 0;

// Controller cvars:
static cvar_t* joy_enable;
static cvar_t* joy_deadzone;
static cvar_t* joy_sensitivity_yaw;
static cvar_t* joy_sensitivity_pitch;
static cvar_t* joy_sensitivity_move;
static cvar_t* joy_trigger_threshold;
static cvar_t* joy_invert_y;
static cvar_t* joy_layout; // 0 = default, 1 = southpaw

// Accumulated stick state (reset each move frame).
static float joy_axis_lx;
static float joy_axis_ly;
static float joy_axis_rx;
static float joy_axis_ry;
static float joy_trigger_left;
static float joy_trigger_right;

// Translates an SDL gamepad button to a Quake 2 key.
static int IN_TranslateGamepadButton(const SDL_GamepadButton button)
{
	switch (button)
	{
		case SDL_GAMEPAD_BUTTON_SOUTH:			return K_JOY1;	// A / Cross
		case SDL_GAMEPAD_BUTTON_EAST:			return K_JOY2;	// B / Circle
		case SDL_GAMEPAD_BUTTON_WEST:			return K_JOY3;	// X / Square
		case SDL_GAMEPAD_BUTTON_NORTH:			return K_JOY4;	// Y / Triangle
		case SDL_GAMEPAD_BUTTON_BACK:			return K_AUX1;
		case SDL_GAMEPAD_BUTTON_GUIDE:			return K_AUX2;
		case SDL_GAMEPAD_BUTTON_START:			return K_AUX3;
		case SDL_GAMEPAD_BUTTON_LEFT_STICK:		return K_AUX4;
		case SDL_GAMEPAD_BUTTON_RIGHT_STICK:	return K_AUX5;
		case SDL_GAMEPAD_BUTTON_LEFT_SHOULDER:	return K_AUX6;
		case SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER:	return K_AUX7;
		case SDL_GAMEPAD_BUTTON_DPAD_UP:		return K_AUX8;
		case SDL_GAMEPAD_BUTTON_DPAD_DOWN:		return K_AUX9;
		case SDL_GAMEPAD_BUTTON_DPAD_LEFT:		return K_AUX10;
		case SDL_GAMEPAD_BUTTON_DPAD_RIGHT:		return K_AUX11;
		default:								return 0;
	}
}

// Applies deadzone and normalizes axis value to -1.0 .. 1.0.
static float IN_ApplyDeadzone(const float value, const float deadzone)
{
	if (fabsf(value) < deadzone)
		return 0.0f;

	// Rescale from deadzone..1.0 to 0.0..1.0.
	const float sign = (value > 0.0f) ? 1.0f : -1.0f;
	return sign * (fabsf(value) - deadzone) / (1.0f - deadzone);
}

static void IN_InitController(void)
{
	joy_enable = Cvar_Get("joy_enable", "1", CVAR_ARCHIVE);
	joy_deadzone = Cvar_Get("joy_deadzone", "0.2", CVAR_ARCHIVE);
	joy_sensitivity_yaw = Cvar_Get("joy_sensitivity_yaw", "240", CVAR_ARCHIVE);
	joy_sensitivity_pitch = Cvar_Get("joy_sensitivity_pitch", "150", CVAR_ARCHIVE);
	joy_sensitivity_move = Cvar_Get("joy_sensitivity_move", "1.0", CVAR_ARCHIVE);
	joy_trigger_threshold = Cvar_Get("joy_trigger_threshold", "0.12", CVAR_ARCHIVE);
	joy_invert_y = Cvar_Get("joy_invert_y", "0", CVAR_ARCHIVE);
	joy_layout = Cvar_Get("joy_layout", "0", CVAR_ARCHIVE);

	joy_axis_lx = 0.0f;
	joy_axis_ly = 0.0f;
	joy_axis_rx = 0.0f;
	joy_axis_ry = 0.0f;
	joy_trigger_left = 0.0f;
	joy_trigger_right = 0.0f;
}

static void IN_StartupController(void) // YQ2: IN_Controller_Init().
{
	if (!(int)joy_enable->value)
	{
		Com_Printf("Gamepad input disabled by joy_enable cvar.\n");
		return;
	}

	if (!SDL_WasInit(SDL_INIT_GAMEPAD))
	{
		if (!SDL_Init(SDL_INIT_GAMEPAD))
		{
			Com_Printf("Couldn't initialize SDL gamepad subsystem: %s\n", SDL_GetError());
			return;
		}
	}

	// Try to open the first available gamepad.
	int num_joysticks = 0;
	SDL_JoystickID* joysticks = SDL_GetJoysticks(&num_joysticks);

	if (joysticks != NULL)
	{
		for (int i = 0; i < num_joysticks; i++)
		{
			if (SDL_IsGamepad(joysticks[i]))
			{
				controller = SDL_OpenGamepad(joysticks[i]);
				if (controller != NULL)
				{
					controller_id = joysticks[i];
					Com_Printf("Gamepad opened: %s\n", SDL_GetGamepadName(controller));
					break;
				}
			}
		}

		SDL_free(joysticks);
	}

	if (controller == NULL)
		Com_Printf("No gamepad found.\n");
}

static void IN_ShutdownController(void) // YQ2: IN_Controller_Shutdown().
{
	if (controller != NULL)
	{
		SDL_CloseGamepad(controller);
		controller = NULL;
		controller_id = 0;
	}

	if (SDL_WasInit(SDL_INIT_GAMEPAD))
		SDL_QuitSubSystem(SDL_INIT_GAMEPAD);
}

static void IN_ControllerMove(usercmd_t* cmd)
{
	if (controller == NULL || !(int)joy_enable->value)
		return;

	if (CL_IgnoreInput())
		return;

	const float deadzone = joy_deadzone->value;
	const float move_scale = joy_sensitivity_move->value;

	// Determine stick assignment based on layout.
	float move_x, move_y, look_x, look_y;

	if ((int)joy_layout->value == 1)
	{
		// Southpaw: right stick moves, left stick looks.
		move_x = IN_ApplyDeadzone(joy_axis_rx, deadzone);
		move_y = IN_ApplyDeadzone(joy_axis_ry, deadzone);
		look_x = IN_ApplyDeadzone(joy_axis_lx, deadzone);
		look_y = IN_ApplyDeadzone(joy_axis_ly, deadzone);
	}
	else
	{
		// Default: left stick moves, right stick looks.
		move_x = IN_ApplyDeadzone(joy_axis_lx, deadzone);
		move_y = IN_ApplyDeadzone(joy_axis_ly, deadzone);
		look_x = IN_ApplyDeadzone(joy_axis_rx, deadzone);
		look_y = IN_ApplyDeadzone(joy_axis_ry, deadzone);
	}

	// Apply movement.
	cmd->sidemove += (short)(move_x * move_scale * 127.0f);
	cmd->forwardmove -= (short)(move_y * move_scale * 127.0f);

	// Apply look.
	const float frame_time = cls.rframetime;
	cl.delta_inputangles[YAW] -= look_x * joy_sensitivity_yaw->value * frame_time;

	const float pitch_sign = (int)joy_invert_y->value ? -1.0f : 1.0f;
	cl.delta_inputangles[PITCH] += look_y * joy_sensitivity_pitch->value * frame_time * pitch_sign;

	// Reset accumulated axis values.
	joy_axis_lx = 0.0f;
	joy_axis_ly = 0.0f;
	joy_axis_rx = 0.0f;
	joy_axis_ry = 0.0f;
	joy_trigger_left = 0.0f;
	joy_trigger_right = 0.0f;
}

// Fires virtual arrow-key events from the left stick while a menu is open.
// Supports an initial delay then a repeat rate, like keyboard auto-repeat.
#define JOY_MENU_NAV_INITIAL	300	// ms before first repeat after initial press.
#define JOY_MENU_NAV_REPEAT		150	// ms between subsequent repeats.

static void IN_ControllerMenuNav(void)
{
	if (controller == NULL || !(int)joy_enable->value || cls.key_dest != key_menu)
		return;

	const float deadzone = joy_deadzone->value;
	const float nav_y = IN_ApplyDeadzone(joy_axis_ly, deadzone);
	const float nav_x = IN_ApplyDeadzone(joy_axis_lx, deadzone);

	// [0]=up, [1]=down, [2]=left, [3]=right
	static const int nav_keys[4] = { K_UPARROW, K_DOWNARROW, K_LEFTARROW, K_RIGHTARROW };
	static int nav_next[4];	// Next curtime at which to fire; 0 = direction not active.

	const qboolean active[4] = { nav_y < 0.0f, nav_y > 0.0f, nav_x < 0.0f, nav_x > 0.0f };

	for (int i = 0; i < 4; i++)
	{
		if (active[i])
		{
			if (nav_next[i] == 0)
			{
				// First frame in this direction: fire immediately, then set initial delay.
				Key_Event(nav_keys[i], true, (uint)curtime);
				Key_Event(nav_keys[i], false, (uint)curtime);
				nav_next[i] = curtime + JOY_MENU_NAV_INITIAL;
			}
			else if (curtime >= nav_next[i])
			{
				Key_Event(nav_keys[i], true, (uint)curtime);
				Key_Event(nav_keys[i], false, (uint)curtime);
				nav_next[i] = curtime + JOY_MENU_NAV_REPEAT;
			}
		}
		else
		{
			nav_next[i] = 0;
		}
	}
}

#pragma endregion

void IN_Init(void)
{
	IN_InitMouse();
	IN_InitController();

	if (!SDL_WasInit(SDL_INIT_EVENTS) && !SDL_Init(SDL_INIT_EVENTS))
		Com_Error(ERR_FATAL, "Couldn't initialize SDL event subsystem: %s\n", SDL_GetError());

	IN_StartupController();
}

// Shuts the backend down.
void IN_Shutdown(void)
{
	Com_Printf("Shutting down input.\n");

	IN_ShutdownMouse();
	IN_ShutdownController();

	if (SDL_WasInit(SDL_INIT_EVENTS))
		SDL_QuitSubSystem(SDL_INIT_EVENTS);
}

// Updates the input queue state. Called every frame by the client and does nearly all the input magic.
void IN_Update(void) // YQ2
{
#define NANOSECONDS_IN_MILLISECOND	1000000
	SDL_Event event;

	// Get and process an event.
	while (SDL_PollEvent(&event))
	{
		switch (event.type)
		{
			case SDL_EVENT_MOUSE_WHEEL:
			{
				const int key = (event.wheel.y > 0 ? K_MWHEELUP : K_MWHEELDOWN);
				const uint time = (uint)(event.wheel.timestamp / NANOSECONDS_IN_MILLISECOND);
				Key_Event(key, true, time);
				Key_Event(key, false, time);
			} break;

			case SDL_EVENT_MOUSE_BUTTON_DOWN:
			case SDL_EVENT_MOUSE_BUTTON_UP:
			{
				int key;

				switch (event.button.button)
				{
					case SDL_BUTTON_LEFT:
						key = K_MOUSE1;
						break;

					case SDL_BUTTON_MIDDLE:
						key = K_MOUSE3;
						break;

					case SDL_BUTTON_RIGHT:
						key = K_MOUSE2;
						break;

					default:
						return;
				}

				const uint time = (uint)(event.button.timestamp / NANOSECONDS_IN_MILLISECOND);
				Key_Event(key, (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN), time);
			} break;

			case SDL_EVENT_MOUSE_MOTION:
				if (cls.key_dest == key_game && !CL_PAUSED && !CL_FREEZEWORLD)
				{
					mouse_x += event.motion.xrel;
					mouse_y += event.motion.yrel;
				}
				break;

			case SDL_EVENT_KEY_DOWN:
			case SDL_EVENT_KEY_UP:
			{
				const qboolean down = (event.type == SDL_EVENT_KEY_DOWN);

				// Workaround for AZERTY-keyboards, which don't have 1, 2, ..., 9, 0 in first row:
				// always map those physical keys (scancodes) to those keycodes anyway. See: https://bugzilla.libsdl.org/show_bug.cgi?id=3188
				const SDL_Scancode sc = event.key.scancode;
				const uint time = (uint)(event.key.timestamp / NANOSECONDS_IN_MILLISECOND);

				if (sc >= SDL_SCANCODE_1 && sc <= SDL_SCANCODE_0)
				{
					// Note that the SDL_SCANCODEs are SDL_SCANCODE_1, _2, ..., _9, SDL_SCANCODE_0 while in ASCII it's '0', '1', ..., '9' => handle 0 and 1-9 separately.
					// (quake2 uses the ASCII values for those keys).
					int key = '0'; // Implicitly handles SDL_SCANCODE_0.

					if (sc <= SDL_SCANCODE_9)
						key = '1' + (sc - SDL_SCANCODE_1);

					Key_Event(key, down, time);
				}
				else
				{
					const SDL_Keycode kc = event.key.key;

					if (kc >= SDLK_SPACE && kc < SDLK_DELETE)
					{
						Key_Event((int)kc, down, time);
					}
					else
					{
						const int key = IN_TranslateSDLtoQ2Key(kc);

						if (key != 0)
							Key_Event(key, down, time);
						else
							Com_DPrintf("Pressed unknown key with SDL_Keycode %d, SDL_Scancode %d.\n", kc, (int)sc);
					}
				}
			} break;

			case SDL_EVENT_WINDOW_FOCUS_LOST:
				Key_ClearStates();
				se.Activate(false); //mxd. Also deactivates music backend.
				break;

			case SDL_EVENT_WINDOW_FOCUS_GAINED:
				se.Activate(true); //mxd. Also activates music backend.
				break;

			case SDL_EVENT_WINDOW_SHOWN:
				cls.disable_screen = false; // H2
				break;

			case SDL_EVENT_WINDOW_HIDDEN:
				cls.disable_screen = true; // H2
				break;

			case SDL_EVENT_QUIT:
				Com_Quit();
				break;

				// Gamepad events:
				case SDL_EVENT_GAMEPAD_BUTTON_DOWN:
				case SDL_EVENT_GAMEPAD_BUTTON_UP:
				{
					const int key = IN_TranslateGamepadButton((SDL_GamepadButton)event.gbutton.button);
					if (key != 0)
					{
						const uint time = (uint)(event.gbutton.timestamp / NANOSECONDS_IN_MILLISECOND);
						Key_Event(key, (event.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN), time);
					}
				} break;

				case SDL_EVENT_GAMEPAD_AXIS_MOTION:
				{
					const float value = event.gaxis.value / 32767.0f;

					switch (event.gaxis.axis)
					{
						case SDL_GAMEPAD_AXIS_LEFTX:	joy_axis_lx = value; break;
						case SDL_GAMEPAD_AXIS_LEFTY:	joy_axis_ly = value; break;
						case SDL_GAMEPAD_AXIS_RIGHTX:	joy_axis_rx = value; break;
						case SDL_GAMEPAD_AXIS_RIGHTY:	joy_axis_ry = value; break;

						case SDL_GAMEPAD_AXIS_LEFT_TRIGGER:
						{
							const qboolean was_down = (joy_trigger_left >= joy_trigger_threshold->value);
							joy_trigger_left = value;
							const qboolean is_down = (value >= joy_trigger_threshold->value);

							if (is_down != was_down)
							{
								const uint time = (uint)(event.gaxis.timestamp / NANOSECONDS_IN_MILLISECOND);
								Key_Event(K_AUX12, is_down, time);
							}
						} break;

						case SDL_GAMEPAD_AXIS_RIGHT_TRIGGER:
						{
							const qboolean was_down = (joy_trigger_right >= joy_trigger_threshold->value);
							joy_trigger_right = value;
							const qboolean is_down = (value >= joy_trigger_threshold->value);

							if (is_down != was_down)
							{
								const uint time = (uint)(event.gaxis.timestamp / NANOSECONDS_IN_MILLISECOND);
								Key_Event(K_AUX13, is_down, time);
							}
						} break;
					}
				} break;

				case SDL_EVENT_GAMEPAD_ADDED:
				{
					if (controller == NULL)
					{
						controller = SDL_OpenGamepad(event.gdevice.which);
						if (controller != NULL)
						{
							controller_id = event.gdevice.which;
							Com_Printf("Gamepad connected: %s\n", SDL_GetGamepadName(controller));
						}
					}
				} break;

				case SDL_EVENT_GAMEPAD_REMOVED:
				{
					if (controller != NULL && event.gdevice.which == controller_id)
					{
						Com_Printf("Gamepad disconnected.\n");
						SDL_CloseGamepad(controller);
						controller = NULL;
						controller_id = 0;
					}
				} break;
		}
	}

	// Drive menu navigation from the left stick when a menu is open.
	IN_ControllerMenuNav();

	// Grab and ungrab the mouse if the console is opened.
	// Calling GLimp_GrabInput() each frame is a bit ugly but simple and should work.
	// The called SDL functions return after a cheap check, if there's nothing to do.
	GLimp_GrabInput(IN_ShouldGrabInput());

	// We need to save the frame time so other subsystems know the exact time of the last input events.
	sys_frame_time = curtime; //mxd. Sys_Milliseconds() -> curtime.
}

void IN_Move(usercmd_t* cmd) // Called on packetframe or renderframe.
{
	IN_MouseMove(cmd);
	IN_ControllerMove(cmd);
}

// Removes all pending events from SDLs queue.
void In_FlushQueue(void) // YQ2
{
	SDL_FlushEvents(SDL_EVENT_FIRST, SDL_EVENT_LAST);
	Key_ClearStates();
}

//mxd. Not the best place to put this. Oh well...
inline void Sys_CpuPause(void) //TODO: YQ2 uses __forceinline.
{
	SDL_CPUPauseInstruction();
}