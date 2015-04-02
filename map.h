#define NEIGHBORS_LIMIT 8
#define TRAIN_QUEUE 4

#define PLAYERS_LIMIT 10 /* TODO change to 16 */

#define UNIT_NAME_LIMIT 32

#define REGION_NAME_LIMIT 32

#define REGIONS_LIMIT 256

#define BUILDING_NAME_LIMIT 32

#define SLOT_UNITS 20

#define UNIT_SPEED_LIMIT 16

#define BATTLEFIELD_WIDTH 24
#define BATTLEFIELD_HEIGHT 24

#define BUILDING_WATCH_TOWER (1 << 7)

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
	char name[UNIT_NAME_LIMIT];
	size_t name_length;
	size_t index;

	unsigned char health;
	unsigned char damage;
	unsigned char speed;

	struct resources cost, expense;
	unsigned char time;
	uint32_t requires;

	unsigned char shoot; // damage when shooting
	unsigned char range;
};

struct building
{
	char name[BUILDING_NAME_LIMIT];
	size_t name_length;

	struct resources cost, income;
	unsigned char time;
	uint32_t requires;
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

	size_t index;
	struct region *neighbors[NEIGHBORS_LIMIT];
	struct polygon *location;

	struct point center;

	char name[REGION_NAME_LIMIT];
	size_t name_length;

	uint32_t built;
	signed char construct; // -1 == no construction
	unsigned char construct_time;
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

extern const struct building buildings[];
extern const size_t buildings_count;

int map_init(const union json *restrict json, struct game *restrict game);
void map_term(struct game *restrict game);

void region_income(const struct region* restrict region, struct resources *restrict income);
