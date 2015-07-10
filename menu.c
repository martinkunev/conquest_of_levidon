#include <dirent.h>
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

// Returns a list of NUL-terminated paths to the world files.
// TODO on error, indicate what the error was?
struct files *menu_worlds(const char *restrict path, size_t path_size)
{
	DIR *dir;
	struct dirent *entry, *more;

	size_t count;
	struct files *list = 0;

	entry = malloc(offsetof(struct dirent, d_name) + pathconf(path, _PC_NAME_MAX) + 1);
	if (!entry) return 0;

	dir = opendir(path);
	if (!dir)
	{
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

		name = malloc(offsetof(bytes_t, data) + path_size + name_length + 1);
		if (!name) goto error;
		name->size = path_size + name_length;

		*format_bytes(format_bytes(name->data, path, path_size), entry->d_name, name_length) = 0;

		list->names[list->count++] = name;
	}

	closedir(dir);
	free(entry);

	return list;

error:
	closedir(dir);
	free(entry);

	if (list)
	{
		size_t i;
		for(i = 0; i < list->count; ++i)
			free(list->names[i]);
		free(list);
	}

	return 0;
}

int menu_world_open(const unsigned char *restrict location, size_t location_size, const unsigned char *restrict filename, size_t filename_size, struct game *restrict game)
{
	int file;
	struct stat info;
	unsigned char *buffer;
	int success;
	union json *json;

	// Generate file path.
	unsigned char *filepath = malloc(location_size + filename_size + 1);
	if (!filepath) return ERROR_MEMORY;
	*format_bytes(format_bytes(filepath, location, location_size), filename, filename_size) = 0;

	file = open(filepath, O_RDONLY);
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
