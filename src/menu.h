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

struct files
{
	size_t count;
	struct bytes *names[];
};

#define DIRECTORIES_COUNT 3

int menu_init(void);
void menu_term(void);

struct files *menu_worlds(size_t index);
void menu_free(struct files *list);

int menu_load(size_t index, const unsigned char *restrict filename, size_t filename_size, struct game *restrict game);
int menu_save(size_t index, const unsigned char *restrict filename, size_t filename_size, const struct game *restrict game);
