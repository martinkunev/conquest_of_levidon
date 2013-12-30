#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>

#include "battle.h"

#include <stdio.h>

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
		if (!dmg(pawns, c[0], d[1]) || !dmg(pawns + 1, c[1], d[0])) {*(char *)0 = 0; return 0;} \
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

	if (!dmg(pawns, c[0], d[1]) || !dmg(pawns + 1, c[1], d[0])) *(char *)0 = 0;

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

int main(int argc, char *argv[])
{
	srandom(time(0));

	struct unit peasant = {.health = 3, .damage = 1, .speed = 3};
	s0 = (struct slot){._prev = 0, ._next = 0, .unit = &peasant, .player = 0, .count = 10};
	s1 = (struct slot){._prev = 0, ._next = 0, .unit = &peasant, .player = 5, .count = 10};
	s2 = (struct slot){._prev = 0, ._next = 0, .unit = &peasant, .player = 8, .count = 10};
	s3 = (struct slot){._prev = 0, ._next = 0, .unit = &peasant, .player = 0, .count = 10};

	struct player players[] = {0, 1, 2, 0, 3, 4, 5, 6, 7};
	size_t players_count = sizeof(players) / sizeof(*players);

#define test(n) do \
	{ \
		if (test##n(players, players_count)) write(1, #n ": success\n", 11); \
		else write(1, #n ": error\n", 9); \
	} while (0)

	void if_init(void);

	struct pawn pawns[] = {
		{._prev = 0, ._next = 0, .slot = &s0, .move = {.x[0] = 0, .y[0] = 1, .t = {0, 8}}},
		{._prev = 0, ._next = 0, .slot = &s1, .move = {.x[0] = 6, .y[0] = 1, .t = {0, 8}}},
		{._prev = 0, ._next = 0, .slot = &s2, .move = {.x[0] = 3, .y[0] = 6, .t = {0, 8}}},
	};
	size_t pawns_count = sizeof(pawns) / sizeof(*pawns);

	reset(pawns, pawns_count);

	pawns[0].move.x[1] = 6;
	pawns[0].move.y[1] = 1;

	pawns[1].move.x[1] = 0;
	pawns[1].move.y[1] = 1;

	pawns[2].move.x[1] = 3;
	pawns[2].move.y[1] = 0;

	if_init();
	battle(players, players_count, pawns, pawns_count);

	/*test(7);
	test(8);

	test(0);
	test(1);
	test(2);
	test(3);
	test(4);
	test(5);
	test(6);*/

	//distances(players, players_count);

	return 0;
}
