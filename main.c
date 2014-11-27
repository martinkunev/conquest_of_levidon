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
	int game;
	char *buffer;
	struct string dump;
	union json *json;

	struct player players;
	size_t players_count;

	struct region regions;
	size_t regions_count;

	if (argc < 2)
	{
		write(2, S("You must specify map\n"));
		return 0;
	}

	game = open(argv[1], O_RDONLY);
	if (game < 0) return -1;
	if (fstat(game, &info) < 0)
	{
		close(game);
		return -1;
	}
	buffer = mmap(0, info.st_size, PROT_READ, MAP_SHARED, game, 0);
	close(game);
	if (buffer == MAP_FAILED) return -1;

	dump = string(buffer, info.st_size);
	json = json_parse(&dump);
	munmap(buffer, info.st_size);

	if (!json)
	{
		write(2, S("Invalid map format\n"));
		return -1;
	}
	json_free(json);

	if (map_init(json, &players, players_count, &regions, regions_count))
	{
		write(2, S("Invalid map data\n"));
		return -1;
	}

	srandom(time(0));
	if_init();

	map_play(&players, players_count, &regions, regions_count);

	map_term(&players, players_count, &regions, regions_count);

	return 0;
}
