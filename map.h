#define NEIGHBORS_LIMIT 8
#define TRAIN_QUEUE 4

#define PLAYERS_LIMIT 10 /* TODO change to 16 */

#define REGION_NAME_LIMIT 32

#define REGIONS_LIMIT 256

#define BUILDING_NAME_LIMIT 32

#define BUILDING_IRRIGATION		0x1
#define BUILDING_LUMBERMILL		0x2
#define BUILDING_MINE			0x4

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
	unsigned char time;

	unsigned char shoot; // damage when shooting
	unsigned char range;
};

struct building
{
	char name[BUILDING_NAME_LIMIT];
	size_t name_length;
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

	unsigned char train_time;
	struct unit *train[TRAIN_QUEUE];

	struct slot *slots;
	struct resources income;

	size_t index;
	struct region *neighbors[NEIGHBORS_LIMIT];
	struct polygon *location;

	struct point center;

	char name[REGION_NAME_LIMIT];
	size_t name_length;

	uint32_t buildings, construct;
};

struct game
{
	struct player *players;
	size_t players_count;

	struct region *regions;
	size_t regions_count;

	const struct unit *units;
	size_t units_count;
};

// neighbors (in order): east, north-east, north, north-west, west, south-west, south, south-east

int map_init(const union json *restrict json, struct game *restrict game);
void map_term(struct game *restrict game);
