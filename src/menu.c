#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#if !defined(_DIRENT_HAVE_D_NAMLEN)
# include <string.h>
#endif
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#include "types.h"
#include "base.h"
#include "format.h"
#include "json.h"
#include "map.h"
#include "world.h"
#include "menu.h"

#define DIRECTORY_GAME "/.medieval/"
#define DIRECTORY_SHARED "/share/medieval/worlds/"
#define DIRECTORY_WORLDS "/.medieval/worlds/"
#define DIRECTORY_SAVE "/.medieval/save/"

static bytes_t *directories[DIRECTORIES_COUNT];

static bytes_t *path_cat(const unsigned char *restrict prefix, size_t prefix_size, const unsigned char *restrict suffix, size_t suffix_size)
{
	size_t size;
	bytes_t *path;

	size = prefix_size + suffix_size;
	path = malloc(offsetof(bytes_t, data) + size + 1);
	if (!path) return 0;
	path->size = size;
	*format_bytes(format_bytes(path->data, prefix, prefix_size), suffix, suffix_size) = 0;

	return path;
}

int menu_init(void)
{
	const char *home;
	size_t home_size;

	bytes_t *game_home;

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

	list = malloc(offsetof(struct files, names) + sizeof(bytes_t *) * count);
	if (!list) goto error;
	list->count = 0;

	while (1)
	{
		size_t name_length;
		bytes_t *name;

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

		name = malloc(offsetof(bytes_t, data) + path_size + name_length + 1);
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
	int file;
	struct stat info;
	unsigned char *buffer;
	int success;
	union json *json;

	const unsigned char *restrict location = directories[index]->data;
	size_t location_size = directories[index]->size;

	bytes_t *filepath = path_cat(location, location_size, filename, filename_size);
	if (!filepath) return ERROR_MEMORY;

	file = open(filepath->data, O_RDONLY);
	free(filepath);
	if (file < 0) return -1;
	if (fstat(file, &info) < 0)
	{
		close(file);
		return -1;
	}
	buffer = mmap(0, info.st_size, PROT_READ, MAP_SHARED, file, 0);
	close(file);
	if (buffer == MAP_FAILED) return -1;

	json = json_parse(buffer, info.st_size);
	munmap(buffer, info.st_size);

	if (!json) return ERROR_INPUT;
	success = !world_load(json, game);
	json_free(json);
	if (!success) return ERROR_INPUT;

	return 0;
}

int menu_save(size_t index, const unsigned char *restrict filename, size_t filename_size, const struct game *restrict game)
{
	int file;
	union json *json;
	unsigned char *buffer;
	size_t size, progress;
	ssize_t written;

	const unsigned char *restrict location = directories[index]->data;
	size_t location_size = directories[index]->size;

	json = world_save(game);
	if (!json) return ERROR_MEMORY;

	size = json_size(json);
	buffer = malloc(size);
	if (!buffer)
	{
		json_free(json);
		return ERROR_MEMORY;
	}

	json_dump(buffer, json);
	json_free(json);

	bytes_t *filepath = path_cat(location, location_size, filename, filename_size);
	if (!filepath)
	{
		free(buffer);
		return ERROR_MEMORY;
	}

	file = creat(filepath->data, 0644);
	if (file < 0)
	{
		free(filepath);
		free(buffer);
		return -1;
	}

	// Write the serialized world into the file.
	for(progress = 0; progress < size; progress += written)
	{
		written = write(file, buffer + progress, size - progress);
		if (written < 0)
		{
			unlink(filepath->data);
			close(file);
			free(filepath);
			free(buffer);
			return -1;
		}
	}

	close(file);
	free(filepath);
	free(buffer);
	return 0;
}
