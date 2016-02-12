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

#include <dirent.h>
#include <errno.h>
#include <stdlib.h>
#if !defined(_DIRENT_HAVE_D_NAMLEN)
# include <string.h>
#endif
#include <sys/stat.h>
#include <unistd.h>

#include "errors.h"
#include "base.h"
#include "format.h"
#include "json.h"
#include "map.h"
#include "world.h"
#include "menu.h"

#define DIRECTORY_GAME "/.conquest_of_levidon/"
#define DIRECTORY_SHARED "/share/conquest_of_levidon/worlds/"
#define DIRECTORY_WORLDS "/.conquest_of_levidon/worlds/"
#define DIRECTORY_SAVE "/.conquest_of_levidon/save/"

static struct bytes *directories[DIRECTORIES_COUNT];

static struct bytes *path_cat(const unsigned char *restrict prefix, size_t prefix_size, const unsigned char *restrict suffix, size_t suffix_size)
{
	size_t size;
	struct bytes *path;

	size = prefix_size + suffix_size;
	path = malloc(offsetof(struct bytes, data) + size + 1);
	if (!path) return 0;
	path->size = size;
	*format_bytes(format_bytes(path->data, prefix, prefix_size), suffix, suffix_size) = 0;

	return path;
}

int menu_init(void)
{
	const char *home;
	size_t home_size;

	struct bytes *game_home;

	home = getenv("HOME");
	if (!home) return ERROR_MISSING;
	home_size = strlen(home);

	// TODO check for very large values of home_size

	game_home = path_cat(home, home_size, DIRECTORY_GAME, sizeof(DIRECTORY_GAME) - 1);
	if (!game_home) return ERROR_MEMORY;
	if ((mkdir(game_home->data, 0755) < 0) && (errno != EEXIST))
	{
		free(game_home);
		return ERROR_ACCESS;
	}
	free(game_home);

	directories[0] = path_cat(PREFIX, sizeof(PREFIX) - 1, DIRECTORY_SHARED, sizeof(DIRECTORY_SHARED) - 1);
	if (!directories[0]) return ERROR_MEMORY;

	directories[1] = path_cat(home, home_size, DIRECTORY_WORLDS, sizeof(DIRECTORY_WORLDS) - 1);
	if (!directories[1])
	{
		free(directories[0]);
		return ERROR_MEMORY;
	}

	directories[2] = path_cat(home, home_size, DIRECTORY_SAVE, sizeof(DIRECTORY_SAVE) - 1);
	if (!directories[2])
	{
		free(directories[0]);
		free(directories[1]);
		return ERROR_MEMORY;
	}

	return 0;
}

void menu_term(void)
{
	free(directories[0]);
	free(directories[1]);
	free(directories[2]);
}

// Returns a list of NUL-terminated paths to the world files.
// TODO on error, indicate what the error was?
struct files *menu_worlds(size_t index)
{
	DIR *dir;
	struct dirent *entry, *more;

	size_t count;
	struct files *list = 0;

	const unsigned char *restrict path = directories[index]->data;
	size_t path_size = directories[index]->size;

	entry = malloc(offsetof(struct dirent, d_name) + pathconf(path, _PC_NAME_MAX) + 1);
	if (!entry) return 0;

	dir = opendir(path);
	while (!dir)
	{
		// If the directory does not exist, try to create and open it.
		if ((errno == ENOENT) && !mkdir(path, 0755) && (dir = opendir(path)))
			break;

		free(entry);
		return 0;
	}

	// Count the files in the directory.
	count = 0;
	while (1)
	{
		if (readdir_r(dir, entry, &more)) goto error;
		if (!more) break; // no more entries

		// Skip hidden files.
		if (entry->d_name[0] == '.') continue;

		count += 1;
	}
	rewinddir(dir);

	list = malloc(offsetof(struct files, names) + sizeof(struct bytes *) * count);
	if (!list) goto error;
	list->count = 0;

	while (1)
	{
		size_t name_length;
		struct bytes *name;

		if (readdir_r(dir, entry, &more)) goto error;
		if (!more) break; // no more entries

		// Skip hidden files.
		if (entry->d_name[0] == '.') continue;

#if defined(_DIRENT_HAVE_D_NAMLEN)
		name_length = entry->d_namlen;
#else
		name_length = strlen(entry->d_name);
#endif

		// TODO don't allocate memory for the path

		name = malloc(offsetof(struct bytes, data) + path_size + name_length + 1);
		if (!name) goto error;
		//name->size = path_size + name_length;
		name->size = name_length;

		//*format_bytes(format_bytes(name->data, path, path_size), entry->d_name, name_length) = 0;
		*format_bytes(name->data, entry->d_name, name_length) = 0;

		list->names[list->count++] = name;
	}

	closedir(dir);
	free(entry);

	return list;

error:
	closedir(dir);
	free(entry);

	menu_free(list);

	return 0;
}

void menu_free(struct files *list)
{
	if (list)
	{
		size_t i;
		for(i = 0; i < list->count; ++i)
			free(list->names[i]);
		free(list);
	}
}

int menu_load(size_t index, const unsigned char *restrict filename, size_t filename_size, struct game *restrict game)
{
	int status;

	const unsigned char *restrict location = directories[index]->data;
	size_t location_size = directories[index]->size;

	struct bytes *filepath = path_cat(location, location_size, filename, filename_size);
	if (!filepath) return ERROR_MEMORY;

	status = world_load(filepath->data, game);
	free(filepath);

	return status;
}

int menu_save(size_t index, const unsigned char *restrict filename, size_t filename_size, const struct game *restrict game)
{
	int status;

	const unsigned char *restrict location = directories[index]->data;
	size_t location_size = directories[index]->size;

	struct bytes *filepath = path_cat(location, location_size, filename, filename_size);
	if (!filepath) return ERROR_MEMORY;

	status = world_save(game, filepath->data);
	free(filepath);

	return status;
}
