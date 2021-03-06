/*
 * Conquest of Levidon
 * Copyright (C) 2016  Martin Kunev <martinkunev@gmail.com>
 *
 * This file is part of Conquest of Levidon.
 *
 * Conquest of Levidon is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation version 3 of the License.
 *
 * Conquest of Levidon is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with Conquest of Levidon.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <stdlib.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>

#include <X11/Xlib.h>
#include <X11/keysym.h>
#include <xcb/xcb.h>

#include "errors.h"
#include "game.h"
#include "input.h"
#include "interface.h"

extern xcb_connection_t *connection;
extern KeySym *keymap;
extern int keysyms_per_keycode;
extern int keycode_min, keycode_max;

static int is_modifier(int code)
{
	switch (code)
	{
	default:
		return 0;

	case XK_Shift_L:
	case XK_Shift_R:
	case XK_Control_L:
	case XK_Control_R:
	case XK_Alt_L:
	case XK_Alt_R:
	case XK_Super_L:
	case XK_Super_R:
	case XK_Caps_Lock:
	case XK_Num_Lock:
		return 1;
	}
}

int input_finish(int code, unsigned x, unsigned y, uint16_t modifiers, const struct game *restrict game, void *argument)
{
	if (code != EVENT_MOUSE_LEFT) return INPUT_NOTME;
	return INPUT_FINISH;
}

int input_terminate(int code, unsigned x, unsigned y, uint16_t modifiers, const struct game *restrict game, void *argument)
{
	if (code != EVENT_MOUSE_LEFT) return INPUT_NOTME;
	return INPUT_TERMINATE;
}

// ERROR_CANCEL - user-specified termination
int input_local(const struct area *restrict areas, size_t areas_count, void (*display)(const void *, const struct game *), const struct game *restrict game, void *state)
{
	xcb_generic_event_t *event;
	xcb_button_release_event_t *mouse;
	xcb_key_press_event_t *keyboard;
	xcb_motion_notify_event_t *motion;

	// TODO support capital letters with shift and caps lock

	int code; // TODO this is oversimplification
	unsigned x, y;
	uint16_t modifiers;

	size_t index;
	int status;

	// Ignore all the queued events.
	while (event = xcb_poll_for_event(connection))
		free(event);

	input_display(display, game, state);

	while (1)
	{
wait:
		event = xcb_wait_for_event(connection);
		if (!event) return ERROR_MEMORY;

		switch (event->response_type & ~0x80)
		{
		case XCB_BUTTON_PRESS:
			mouse = (xcb_button_release_event_t *)event;
			code = -mouse->detail;
			x = mouse->event_x;
			y = mouse->event_y;
			modifiers = mouse->state;
			break;

		case XCB_KEY_PRESS:
			keyboard = (xcb_key_press_event_t *)event;
			code = keymap[(keyboard->detail - keycode_min) * keysyms_per_keycode];
			if (is_modifier(code)) continue;
			x = keyboard->event_x;
			y = keyboard->event_y;
			modifiers = keyboard->state;
			break;

		case XCB_MOTION_NOTIFY:
			motion = (xcb_motion_notify_event_t *)event;
			code = EVENT_MOTION;
			x = motion->event_x;
			y = motion->event_y;
			modifiers = motion->state;
			break;

		case XCB_EXPOSE:
			input_display(display, game, state);
		default:
			free(event);
			continue;
		}

		free(event);

		// Propagate the event until someone handles it.
		index = areas_count - 1;
		do
		{
			if ((areas[index].left <= x) && (x <= areas[index].right) && (areas[index].top <= y) && (y <= areas[index].bottom))
			{
				status = areas[index].callback(code, x - areas[index].left, y - areas[index].top, modifiers, game, state);
				switch (status)
				{
				case INPUT_TERMINATE:
					status = ERROR_CANCEL;
				default: // runtime error
					return status;

				case INPUT_FINISH:
					return 0;

				case INPUT_NOTME:
					continue;

				case 0:
					input_display(display, game, state);
				case INPUT_IGNORE:
					goto wait;
				}
			}
		} while (index--);
	}
}

static double timer_progress(const struct timeval *restrict start, double duration)
{
	struct timeval now;
	unsigned long time_difference;
	gettimeofday(&now, 0);
	time_difference = (now.tv_sec * 1000000 + now.tv_usec - start->tv_sec * 1000000 - start->tv_usec);
	return time_difference / (duration * 1000000.0);
}

int input_timer(void (*display)(const void *, const struct game *, double), const struct game *restrict game, double duration, void *state)
{
	xcb_generic_event_t *event;
	xcb_key_press_event_t *keyboard;

	// TODO support capital letters with shift and caps lock

	int code = 0; // TODO this is oversimplification

	struct timeval start; // start time of the animation

	// Ignore all the queued events.
	while (event = xcb_poll_for_event(connection))
		free(event);

	gettimeofday(&start, 0);

	do
	{
		double progress = timer_progress(&start, duration);
		if (progress > 1.0)
			break;

		input_display_timer(display, game, progress, state);

		event = xcb_poll_for_event(connection);
		if (!event)
		{
			usleep(10000);
			continue;
		}

		switch (event->response_type & ~0x80)
		{
		case XCB_EXPOSE:
		default:
			free(event);
			continue;

		case XCB_KEY_PRESS:
			keyboard = (xcb_key_press_event_t *)event;
			code = keymap[(keyboard->detail - keycode_min) * keysyms_per_keycode];
			break;
		}

		free(event);
	} while (code != XK_Escape);

	return 0;
}
