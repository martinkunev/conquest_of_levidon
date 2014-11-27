#define NEIGHBORS_LIMIT 8
#define TRAIN_QUEUE 4

#define PLAYERS_LIMIT 10 /* TODO change to 16 */

#define REGIONS_LIMIT 256

#include "resources.h"
#include "display.h"

struct player
{
	enum {Neutral, Local, Computer, Remote} type;
	struct resources treasury;
	unsigned char alliance;
};

struct unit
{
	size_t index;

	unsigned char health;
	unsigned char damage;
	unsigned char speed;

	struct resources cost, expense;

	unsigned char shoot; // damage while shooting
	unsigned char range;
};

struct slot
{
	struct slot *_prev, *_next;
	const struct unit *unit;
	unsigned count;
	unsigned char player;

	struct region *location, *move; // TODO change this to *location[2]
};

struct region
{
	unsigned char owner;
	struct slot *slots;
	struct unit *train[TRAIN_QUEUE];
	struct resources income;

	size_t index;
	struct region *neighbors[NEIGHBORS_LIMIT];
	struct polygon *location;

	struct point center;
};

// neighbors (in order): east, north-east, north, north-west, west, south-west, south, south-east

int map_init(const union json *restrict json, struct player *restrict players, size_t players_count, struct region *restrict regions, size_t regions_count);
void map_play(struct player *restrict players, size_t players_count, struct region *restrict regions, size_t regions_count);
void map_term(struct player *restrict players, size_t players_count, struct region *restrict regions, size_t regions_count);
