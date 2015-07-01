#define NEIGHBORS_LIMIT 8
#define TRAIN_QUEUE 4

#define PLAYERS_LIMIT 16

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

enum weapon {WEAPON_NONE, WEAPON_ARROW, WEAPON_CLEAVING, WEAPON_POLEARM, WEAPON_BLADE, WEAPON_BLUNT};
enum armor {ARMOR_NONE, ARMOR_LEATHER, ARMOR_CHAINMAIL, ARMOR_PLATE, ARMOR_WOODEN, ARMOR_STONE};

struct unit
{
	char name[NAME_LIMIT];
	size_t name_length;

	size_t index;

	struct resources cost, expense;
	uint32_t requires;
	unsigned char time;

	unsigned char speed;
	unsigned char health;
	enum armor armor;

	struct
	{
		double agility;
		enum weapon weapon;
		unsigned char damage;
	} melee;
	struct
	{
		enum weapon weapon;
		unsigned char damage;
		unsigned char range;
	} ranged;
};

enum {BuildingFarm, BuildingIrrigation, BuildingSawmill, BuildingMine, BuildingBlastFurnace, BuildingBarracks, BuildingArcheryRange, BuildingStables, BuildingWatchTower, BuildingPalisade, BuildingFortress, BuildingWorkshop};

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
	unsigned char owner;

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

		int assault;
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

enum {PALISADE, FORTRESS};
struct garrison_info
{
	size_t index;

	unsigned troops;
	unsigned provisions;
	unsigned strength_wall, strength_gate;
};

static inline const struct garrison_info *garrison_info(const struct region *restrict region)
{
	static const struct garrison_info info[] =
	{
		[PALISADE] = {.index = PALISADE, .troops = 3, .provisions = 2, .strength_wall = 100, .strength_gate = 40},
		[FORTRESS] = {.index = FORTRESS, .troops = 6, .provisions = 5, .strength_wall = 200, .strength_gate = 60},
	};

	if (region_built(region, BuildingFortress)) return info + FORTRESS;
	else if (region_built(region, BuildingPalisade)) return info + PALISADE;
	else return 0;
}

extern const struct unit UNITS[];
extern const size_t UNITS_COUNT;

extern const struct building buildings[];
extern const size_t buildings_count;

void region_income(const struct region* restrict region, struct resources *restrict income);

void troop_attach(struct troop **troops, struct troop *troop);
void troop_detach(struct troop **troops, struct troop *troop);
void troop_remove(struct troop **troops, struct troop *troop);

int troop_spawn(struct region *restrict region, struct troop **restrict troops, const struct unit *restrict unit, unsigned count, unsigned char owner);

void map_visible(const struct game *restrict game, unsigned char player, unsigned char visible[REGIONS_LIMIT]);

static inline int allies(const struct game *game, unsigned player0, unsigned player1)
{
	return (game->players[player0].alliance == game->players[player1].alliance);
}
