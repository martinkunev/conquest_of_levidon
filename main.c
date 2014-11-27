#include <fcntl.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include "types.h"
#include "json.h"
#include "map.h"
#include "battle.h"
#include "interface.h"

#define S(s) s, sizeof(s) - 1

int main(int argc, char *argv[])
{
	struct stat info;
	int file;
	char *buffer;
	struct string dump;
	union json *json;

	struct game game;

	if (argc < 2)
	{
		write(2, S("You must specify map\n"));
		return 0;
	}

	file = open(argv[1], O_RDONLY);
	if (file < 0) return -1;
	if (fstat(file, &info) < 0)
	{
		close(file);
		return -1;
	}
	buffer = mmap(0, info.st_size, PROT_READ, MAP_SHARED, file, 0);
	close(file);
	if (buffer == MAP_FAILED) return -1;

	dump = string(buffer, info.st_size);
	json = json_parse(&dump);
	munmap(buffer, info.st_size);

	if (!json)
	{
		write(2, S("Invalid map format\n"));
		return -1;
	}
	if (map_init(json, &game))
	{
		json_free(json);
		write(2, S("Invalid map data\n"));
		return -1;
	}
	json_free(json);

	srandom(time(0));
	if_init();

	map_play(game.players, game.players_count, game.regions, game.regions_count);

	map_term(game.players, game.players_count, game.regions, game.regions_count);

	return 0;
}
