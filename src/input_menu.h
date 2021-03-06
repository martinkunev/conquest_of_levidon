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

#define FILENAME_LIMIT 64

struct state
{
	struct files *worlds;
	ssize_t world_index;

	size_t directory;

	char name[FILENAME_LIMIT];
	size_t name_size;
	size_t name_position;

	const char *error;
	size_t error_size;

	int loaded;
};

int input_load(struct game *restrict game);
int input_save(const struct game *restrict game);
