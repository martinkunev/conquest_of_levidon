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

enum
{
	INPUT_HANDLED = 0,	// event handled
	INPUT_NOTME,		// pass the event to the next handler
	INPUT_IGNORE,		// ignore event
	INPUT_FINISH,		// don't wait for more input
	INPUT_TERMINATE,	// user requests termination
};

#define EVENT_MOUSE_LEFT -1
#define EVENT_MOUSE_RIGHT -3
#define EVENT_MOTION -127

struct area
{
	unsigned left, right, top, bottom;
	int (*callback)(int, unsigned, unsigned, uint16_t, const struct game *restrict, void *);
	// TODO ? event mask
};

int input_finish(int code, unsigned x, unsigned y, uint16_t modifiers, const struct game *restrict game, void *argument);
int input_terminate(int code, unsigned x, unsigned y, uint16_t modifiers, const struct game *restrict game, void *argument);

int input_local(const struct area *restrict areas, size_t areas_count, void (*display)(const void *, const struct game *), const struct game *restrict game, void *state);
int input_timer(void (*display)(const void *, const struct game *, double), const struct game *restrict game, double duration, void *state);
