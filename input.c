#include <stdlib.h>

#include <xcb/xcb.h>

#include "types.h"
#include "map.h"
#include "input.h"

extern xcb_connection_t *connection;
extern KeySym *keymap;
extern int keysyms_per_keycode;
extern int keycode_min, keycode_max;

int input_local(const struct area *restrict areas, size_t areas_count, void (*display)(const void *, const struct game *), const struct game *restrict game, void *state)
{
	xcb_generic_event_t *event;
	xcb_button_release_event_t *mouse;
	xcb_key_press_event_t *keyboard;
	xcb_motion_notify_event_t *motion;

	int code; // TODO this is oversimplification
	unsigned x, y;
	uint16_t modifiers;

	size_t index;
	int status;

	display(state, game); // TODO is this necessary?

	// TODO clear queued events (previously pressed keys, etc.)

	while (1)
	{
		// TODO consider using xcb_poll_for_event()
		event = xcb_wait_for_event(connection);
		if (!event) return ERROR_MEMORY;

		switch (event->response_type & ~0x80)
		{
		case XCB_EXPOSE:
			display(state, game);
			continue;

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
			x = keyboard->event_x;
			y = keyboard->event_y;
			modifiers = keyboard->state;
			break;

			//KeySym *input = keymap + (keyboard->detail - keycode_min) * keysyms_per_keycode;
			//printf("%d %c %c %c %c\n", (int)*input, (int)input[0], (int)input[1], (int)input[2], (int)input[3]);

		case XCB_MOTION_NOTIFY:
			motion = (xcb_motion_notify_event_t *)event;
			code = EVENT_MOTION;
			x = motion->event_x;
			y = motion->event_y;
			modifiers = motion->state;
			break;

		default:
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
					return -1;
				case INPUT_DONE:
					return 0;
				case INPUT_NOTME:
					continue;
				}
				break;
			}
		} while (index--);
		display(state, game);
	}
}
