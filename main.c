#include <fcntl.h>
#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include "types.h"
#include "json.h"
#include "battle.h"
#include "interface.h"

#include <stdio.h>

struct unit peasant = {.health = 3, .damage = 1, .speed = 3, .cost = {.gold = 1, .food = 2}};

void reset(struct pawn *restrict pawns, size_t pawns_count)
{
	// By default, each pawn stays at its place.
	size_t i;
	for(i = 0; i < pawns_count; ++i)
	{
		pawns[i].move.x[1] = pawns[i].move.x[0];
		pawns[i].move.y[1] = pawns[i].move.y[0];
	}
}

static struct slot s0, s1, s2, s3;

static int pos(struct pawn *p, unsigned char x, unsigned char y)
{
	return ((p->move.x[0] == x) && (p->move.y[0] == y));
}

static int test0(const struct player *restrict players, size_t players_count)
{
	struct pawn pawns[] = {
		{._prev = 0, ._next = 0, .slot = &s0, .move = {.x[0] = 0, .y[0] = 0, .t = {0, 8}}},
		{._prev = 0, ._next = 0, .slot = &s3, .move = {.x[0] = 0, .y[0] = 3, .t = {0, 8}}},
	};
	size_t pawns_count = sizeof(pawns) / sizeof(*pawns);

	reset(pawns, pawns_count);

	pawns[0].move.x[1] = 0;
	pawns[0].move.y[1] = 5;

	battle(players, players_count, pawns, pawns_count);

	return (pos(pawns, 0, 5) && pos(pawns + 1, 0, 3));
}

static int test1(const struct player *restrict players, size_t players_count)
{
	struct pawn pawns[] = {
		{._prev = 0, ._next = 0, .slot = &s0, .move = {.x[0] = 0, .y[0] = 0, .t = {0, 8}}},
		{._prev = 0, ._next = 0, .slot = &s1, .move = {.x[0] = 0, .y[0] = 3, .t = {0, 8}}},
	};
	size_t pawns_count = sizeof(pawns) / sizeof(*pawns);

	reset(pawns, pawns_count);

	pawns[0].move.x[1] = 0;
	pawns[0].move.y[1] = 5;

	battle(players, players_count, pawns, pawns_count);

	return (pos(pawns, 0, 3) && pos(pawns + 1, 0, 3));
}

static int test2(const struct player *restrict players, size_t players_count)
{
	struct pawn pawns[] = {
		{._prev = 0, ._next = 0, .slot = &s0, .move = {.x[0] = 0, .y[0] = 0, .t = {0, 8}}},
		{._prev = 0, ._next = 0, .slot = &s1, .move = {.x[0] = 4, .y[0] = 0, .t = {0, 8}}},
	};
	size_t pawns_count = sizeof(pawns) / sizeof(*pawns);

	reset(pawns, pawns_count);

	pawns[0].move.x[1] = 4;
	pawns[0].move.y[1] = 0;

	pawns[1].move.x[1] = 0;
	pawns[1].move.y[1] = 0;

	battle(players, players_count, pawns, pawns_count);

	return (pos(pawns, 2, 0) && pos(pawns + 1, 2, 0));
}

static int test3(const struct player *restrict players, size_t players_count)
{
	struct pawn pawns[] = {
		{._prev = 0, ._next = 0, .slot = &s0, .move = {.x[0] = 0, .y[0] = 1, .t = {0, 8}}},
		{._prev = 0, ._next = 0, .slot = &s1, .move = {.x[0] = 6, .y[0] = 1, .t = {0, 8}}},
		{._prev = 0, ._next = 0, .slot = &s3, .move = {.x[0] = 3, .y[0] = 6, .t = {0, 8}}},
	};
	size_t pawns_count = sizeof(pawns) / sizeof(*pawns);

	reset(pawns, pawns_count);

	pawns[0].move.x[1] = 6;
	pawns[0].move.y[1] = 1;

	pawns[1].move.x[1] = 0;
	pawns[1].move.y[1] = 1;

	pawns[2].move.x[1] = 3;
	pawns[2].move.y[1] = 0;

	battle(players, players_count, pawns, pawns_count);

	return (pos(pawns, 3, 1) && pos(pawns + 1, 3, 1) && pos(pawns + 2, 3, 1));
}

static int test4(const struct player *restrict players, size_t players_count)
{
	struct pawn pawns[] = {
		{._prev = 0, ._next = 0, .slot = &s0, .move = {.x[0] = 0, .y[0] = 1, .t = {0, 8}}},
		{._prev = 0, ._next = 0, .slot = &s3, .move = {.x[0] = 6, .y[0] = 1, .t = {0, 8}}},
		{._prev = 0, ._next = 0, .slot = &s1, .move = {.x[0] = 3, .y[0] = 6, .t = {0, 8}}},
	};
	size_t pawns_count = sizeof(pawns) / sizeof(*pawns);

	reset(pawns, pawns_count);

	pawns[0].move.x[1] = 6;
	pawns[0].move.y[1] = 1;

	pawns[1].move.x[1] = 0;
	pawns[1].move.y[1] = 1;

	pawns[2].move.x[1] = 3;
	pawns[2].move.y[1] = 0;

	battle(players, players_count, pawns, pawns_count);

	return (pos(pawns, 6, 1) && pos(pawns + 1, 0, 1) && pos(pawns + 2, 3, 0));
}

static int test5(const struct player *restrict players, size_t players_count)
{
	struct pawn pawns[] = {
		{._prev = 0, ._next = 0, .slot = &s0, .move = {.x[0] = 0, .y[0] = 1, .t = {0, 8}}},
		{._prev = 0, ._next = 0, .slot = &s1, .move = {.x[0] = 6, .y[0] = 1, .t = {0, 8}}},
		{._prev = 0, ._next = 0, .slot = &s3, .move = {.x[0] = 3, .y[0] = 0, .t = {0, 8}}},
	};
	size_t pawns_count = sizeof(pawns) / sizeof(*pawns);

	reset(pawns, pawns_count);

	pawns[0].move.x[1] = 6;
	pawns[0].move.y[1] = 1;

	pawns[1].move.x[1] = 0;
	pawns[1].move.y[1] = 1;

	pawns[2].move.x[1] = 3;
	pawns[2].move.y[1] = 6;

	battle(players, players_count, pawns, pawns_count);

	return (pos(pawns, 3, 1) && pos(pawns + 1, 3, 1) && pos(pawns + 2, 3, 6));
}

static int test6(const struct player *restrict players, size_t players_count)
{
	struct pawn pawns[] = {
		{._prev = 0, ._next = 0, .slot = &s0, .move = {.x[0] = 0, .y[0] = 0, .t = {0, 8}}},
		{._prev = 0, ._next = 0, .slot = &s1, .move = {.x[0] = 3, .y[0] = 3, .t = {0, 8}}},
		{._prev = 0, ._next = 0, .slot = &s2, .move = {.x[0] = 0, .y[0] = 0, .t = {0, 8}}},
		{._prev = 0, ._next = 0, .slot = &s3, .move = {.x[0] = 0, .y[0] = 3, .t = {0, 8}}},
	};
	size_t pawns_count = sizeof(pawns) / sizeof(*pawns);

	reset(pawns, pawns_count);

	pawns[0].move.x[1] = 3;
	pawns[0].move.y[1] = 1;

	pawns[1].move.x[1] = 2;
	pawns[1].move.y[1] = 0;

	pawns[2].move.x[1] = 3;
	pawns[2].move.y[1] = 4;

	pawns[3].move.x[1] = 2;
	pawns[3].move.y[1] = 3;

	battle(players, players_count, pawns, pawns_count);

	return (pos(pawns, 2, 1) && pos(pawns + 1, 2, 1) && pos(pawns + 2, 2, 3) && pos(pawns + 3, 2, 3));
}

static int dmg(struct pawn *p, unsigned count, unsigned damage)
{
	return (!p->slot->count || (((count - p->slot->count) * p->slot->unit->health + p->hurt) == damage));
}

#define round() do \
	{ \
		c[0] = pawns[0].slot->count; \
		c[1] = pawns[1].slot->count; \
\
		d[0] = c[0] * pawns[0].slot->unit->damage + pawns[1].hurt; \
		d[1] = c[1] * pawns[1].slot->unit->damage + pawns[0].hurt; \
\
		battle(players, players_count, pawns, pawns_count); \
\
		if (!dmg(pawns, c[0], d[1]) || !dmg(pawns + 1, c[1], d[0])) return 0; \
	} while (0)

static int test7(const struct player *restrict players, size_t players_count)
{
	struct pawn pawns[] = {
		{._prev = 0, ._next = 0, .slot = &s0, .move = {.x[0] = 0, .y[0] = 0, .t = {0, 8}}},
		{._prev = 0, ._next = 0, .slot = &s1, .move = {.x[0] = 0, .y[0] = 0, .t = {0, 8}}},
	};
	size_t pawns_count = sizeof(pawns) / sizeof(*pawns);

	unsigned c[2], d[2];

	reset(pawns, pawns_count);

	round();
	round();
	round();

#if defined(DEBUG)
	printf("0: count=%u hurt=%u\n", pawns[0].slot->count, pawns[0].hurt);
	printf("1: count=%u hurt=%u\n", pawns[1].slot->count, pawns[1].hurt);
#endif

	return 1;
}

static int test8(const struct player *restrict players, size_t players_count)
{
	struct pawn pawns[] = {
		{._prev = 0, ._next = 0, .slot = &s0, .move = {.x[0] = 0, .y[0] = 0, .t = {0, 8}}},
		{._prev = 0, ._next = 0, .slot = &s1, .move = {.x[0] = 0, .y[0] = 0, .t = {0, 8}}},
	};
	size_t pawns_count = sizeof(pawns) / sizeof(*pawns);

	unsigned c[2], d[2];
	int dx, dy;

	reset(pawns, pawns_count);

	pawns[0].move.x[1] = 2;
	pawns[0].move.y[1] = 0;

	c[0] = pawns[0].slot->count;
	c[1] = pawns[1].slot->count;

	dx = (pawns[1].move.x[1] - pawns[1].move.x[0]);
	dy = (pawns[1].move.y[1] - pawns[1].move.y[0]);
	d[0] = (c[0] * pawns[0].slot->unit->damage * sqrt((pawns[1].move.t[1] - pawns[1].move.t[0]) / sqrt(dx * dx + dy * dy)) + pawns[1].hurt);

	dx = (pawns[0].move.x[1] - pawns[0].move.x[0]);
	dy = (pawns[0].move.y[1] - pawns[0].move.y[0]);
	d[1] = (c[1] * pawns[1].slot->unit->damage * sqrt((pawns[0].move.t[1] - pawns[0].move.t[0]) / sqrt(dx * dx + dy * dy)) + pawns[0].hurt);

	battle(players, players_count, pawns, pawns_count);

	return (dmg(pawns, c[0], d[1]) && dmg(pawns + 1, c[1], d[0]));
}

static void distance(const struct player *restrict players, struct pawn *battlefield[BATTLEFIELD_HEIGHT][BATTLEFIELD_WIDTH], const struct pawn *pawn, unsigned char speed)
{
	((struct unit *)pawn->slot->unit)->speed = speed;

	unsigned char x, y;
	for(y = 0; y < BATTLEFIELD_HEIGHT; ++y)
	{
		write(1, "+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+\n", BATTLEFIELD_WIDTH * 2 + 1 + 1);
		write(1, "|", 1);
		for(x = 0; x < BATTLEFIELD_WIDTH; ++x)
		{
			if (reachable(players, battlefield, pawn, x, y)) write(1, "*|", 2);
			else write(1, " |", 2);
		}
		write(1, "\n", 1);
	}
	write(1, "+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+\n", BATTLEFIELD_WIDTH * 2 + 1 + 1);
}

/*static void distances(const struct player *restrict players, size_t players_count)
{
	struct pawn pawns[] = {
		{._prev = 0, ._next = 0, .slot = &s0, .move = {.x[0] = 7, .y[0] = 7, .t = {0, 8}}},
	};
	size_t pawns_count = sizeof(pawns) / sizeof(*pawns);

	reset(pawns, pawns_count);

	struct pawn *battlefield[BATTLEFIELD_HEIGHT][BATTLEFIELD_WIDTH];
	battle_init(battlefield, pawns, pawns_count);

	distance(players, battlefield, pawns, 1);
	distance(players, battlefield, pawns, 2);
	distance(players, battlefield, pawns, 3);
	distance(players, battlefield, pawns, 4);
	distance(players, battlefield, pawns, 5);
	distance(players, battlefield, pawns, 6);
	distance(players, battlefield, pawns, 7);
}*/

#if TEST
int main(int argc, char *argv[])
{
	srandom(time(0));

	struct unit peasant = {.health = 5, .damage = 1, .speed = 3};
	s0 = (struct slot){._prev = 0, ._next = 0, .unit = &peasant, .player = 0, .count = 20};
	s1 = (struct slot){._prev = 0, ._next = 0, .unit = &peasant, .player = 1, .count = 20};
	s2 = (struct slot){._prev = 0, ._next = 0, .unit = &peasant, .player = 2, .count = 20};
	s3 = (struct slot){._prev = 0, ._next = 0, .unit = &peasant, .player = 0, .count = 20};

	//struct player players[] = {0, 1, 2, 0, 3, 4, 5, 6, 7};
	struct player players[] = {{.alliance = 0}, {.alliance = 1}, {.alliance = 2}};
	size_t players_count = sizeof(players) / sizeof(*players);

#define test(n) do \
	{ \
		if (test##n(players, players_count)) write(1, #n ": success\n", 11); \
		else write(1, #n ": error\n", 9); \
	} while (0)

	if_init();

	//test(7);
	//test(8);

	test(0);
	test(1);
	test(2);
	test(3);
	test(4);
	test(5);
	test(6);

	//distances(players, players_count);

	return 0;
}
#else
#include <stdio.h>
int main(int argc, char *argv[])
{
	if (argc < 2)
	{
		fprintf(stderr, "You must specify battle\n");
		return 0;
	}

	srandom(time(0));

	struct unit peasant = {.health = 3, .damage = 1, .speed = 3};

	struct stat info;
	int fight = open(argv[1], O_RDONLY);
	if (fight < 0) return -1;
	if (fstat(fight, &info) < 0) return -1;
	char *buffer = mmap(0, info.st_size, PROT_READ, MAP_SHARED, fight, 0);
	close(fight);
	if (buffer == MAP_FAILED) return -1;

	struct string dump = string(buffer, info.st_size);
	union json *json = json_parse(&dump);
	munmap(buffer, info.st_size);

	if (!json)
	{
		printf("Invalid battle\n");
		return -1;
	}

	struct string key;
	union json *item, *node, *field;
	size_t index;

	struct player *players = 0;
	struct pawn *pawns = 0;
	struct vector *player_pawns = 0;

	if (json_type(json) != OBJECT) goto finally;

	key = string("players");
	node = dict_get(json->object, &key);
	if (!node || (json_type(node) != ARRAY)) goto finally;

	size_t players_count = node->array_node.length;
	players = malloc(players_count * sizeof(struct player));
	if (!players) goto finally;
	player_pawns = malloc(players_count * sizeof(struct vector));
	if (!player_pawns) goto finally;
	for(index = 0; index < players_count; ++index)
	{
		item = node->array_node.data[index];
		if (json_type(item) != INTEGER) goto finally;
		players[index].alliance = item->integer;

		players[index].type = Local;
		players[index].treasury = (struct resources){.gold = 0, .food = 0};

		vector_init(player_pawns + index); // TODO error detection; free memory
	}

	key = string("pawns");
	node = dict_get(json->object, &key);
	if (!node || (json_type(node) != ARRAY)) goto finally;

	size_t pawns_count = node->array_node.length;
	pawns = malloc(pawns_count * (sizeof(struct pawn) + sizeof(struct slot)));
	if (!pawns) goto finally;
	for(index = 0; index < pawns_count; ++index)
	{
		item = node->array_node.data[index];
		if (json_type(item) != OBJECT) goto finally;

		pawns[index]._prev = 0;
		pawns[index]._next = 0;
		pawns[index].slot = (struct slot *)(pawns + pawns_count) + index;

		pawns[index].slot->_prev = 0;
		pawns[index].slot->_next = 0;
		pawns[index].slot->unit = &peasant;

		key = string("player");
		field = dict_get(item->object, &key);
		if (!field || (json_type(field) != INTEGER)) goto finally;
		pawns[index].slot->player = field->integer;

		key = string("count");
		field = dict_get(item->object, &key);
		if (!field || (json_type(field) != INTEGER)) goto finally;
		pawns[index].slot->count = field->integer;

		key = string("x");
		field = dict_get(item->object, &key);
		if (!field || (json_type(field) != INTEGER)) goto finally;
		pawns[index].move.x[1] = pawns[index].move.x[0] = field->integer;

		key = string("y");
		field = dict_get(item->object, &key);
		if (!field || (json_type(field) != INTEGER)) goto finally;
		pawns[index].move.y[1] = pawns[index].move.y[0] = field->integer;

		pawns[index].move.t[0] = 0;
		pawns[index].move.t[1] = 8;

		vector_add(player_pawns + pawns[index].slot->player, pawns + index); // TODO error check; free memory
	}

	if_init();

	battle(players, players_count, pawns, pawns_count);

finally:
	free(pawns);
	free(player_pawns);
	free(players);
	json_free(json);
	return 0;
}
#endif
