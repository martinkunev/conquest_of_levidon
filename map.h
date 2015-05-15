#define NEIGHBORS_LIMIT 8
#define TRAIN_QUEUE 4

#define PLAYERS_LIMIT 10 /* TODO change to 16 */

#define NAME_LIMIT 32

#define REGIONS_LIMIT 256

#define SLOT_UNITS 20

#define UNIT_SPEED_LIMIT 16

#include "resources.h"
#include "display.h"

#define region_unit_available(region, unit) (!((unit).requires & ~(region)->built))
#define region_building_available(region, building) (!((building).requires & ~(region)->built))

#define region_built(region, building) ((int)((region)->built & (1 << (building))))

struct player
{
	enum {Neutral, Local, Computer, Remote} type;
	struct resources treasury;
	unsigned char alliance;
};

struct unit
{
	char name[NAME_LIMIT];
	size_t name_length;

	struct resources cost, expense;
	unsigned char time;
	uint32_t requires;

	size_t index;

	unsigned char health;
	unsigned char damage;
	unsigned char speed;

	unsigned char shoot; // damage when shooting
	unsigned char range;
};

//enum {BuildingFarm, BuildingIrrigation, BuildingSawmill, BuildingMine, BuildingBlastFurnace, BuildingBarracks, BuildingArcheryRange, BuildingStables, BuildingWatchTower, BuildingPalisade, BuildingFortress, BuildingMoat};
enum {BuildingFarm, BuildingIrrigation, BuildingSawmill, BuildingMine, BuildingBlastFurnace, BuildingBarracks, BuildingArcheryRange, BuildingStables, BuildingWatchTower, BuildingPalisade, BuildingFortress};

struct building
{
	char name[NAME_LIMIT];
	size_t name_length;

	struct resources cost, income;
	unsigned char time;
	uint32_t requires;
};

struct troop
{
	struct troop *_prev, *_next;
	const struct unit *unit;
	unsigned count;
	unsigned char player;

	struct region *location, *move; // TODO maybe change this to *location[2]
};

struct region
{
	char name[NAME_LIMIT];
	size_t name_length;

	unsigned char owner;

	unsigned char train_time;
	const struct unit *train[TRAIN_QUEUE];

	struct troop *troops;

	struct
	{
		enum {East, NorthEast, North, NorthWest, West, SouthWest, South, SouthEast} position;

		unsigned char owner;
		struct troop *troops;

		unsigned siege;
	} garrison;

	size_t index;
	struct region *neighbors[NEIGHBORS_LIMIT];
	struct polygon *location;

	struct point center;

	uint32_t built;
	signed char construct; // -1 == no construction
	unsigned char build_progress;
};

struct game
{
	struct player *players;
	size_t players_count;

	struct region *regions;
	size_t regions_count;
};

#define PALISADE 0
#define FORTRESS 1

static const struct
{
	unsigned troops;
	unsigned provisions;
} garrison_info[] =
{
	[PALISADE] = {.troops = 3, .provisions = 2},
	[FORTRESS] = {.troops = 6, .provisions = 5},
};

extern const struct unit UNITS[];
extern const size_t UNITS_COUNT;

extern const struct building buildings[];
extern const size_t buildings_count;

void region_income(const struct region* restrict region, struct resources *restrict income);

void troop_attach(struct troop **troops, struct troop *troop);
void troop_detach(struct troop **troops, struct troop *troop);
void troop_remove(struct troop **troops, struct troop *troop);
