#define NEIGHBORS_LIMIT 8
#define TRAIN_QUEUE 4

#define PLAYERS_LIMIT 16

#define NAME_LIMIT 32

#define REGIONS_LIMIT 256

#define UNIT_SPEED_LIMIT 16

#include "resources.h"
#include "draw.h"

#define region_unit_available(region, unit) (!((unit).requires & ~(region)->built))
#define region_building_available(region, building) (!((building).requires & ~(region)->built))

#define region_built(region, building) ((int)((region)->built & (1 << (building))))

struct player
{
	//enum {Neutral, Local, Computer, Remote} type;
	enum {Neutral, Local} type;
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

	uint32_t requires;
	unsigned troops_count;
	struct resources cost, expense;
	unsigned char time;

	unsigned char speed;
	unsigned char health;
	enum armor armor;

	struct
	{
		double agility;
		enum weapon weapon;
		unsigned char damage; // TODO double?
	} melee;
	struct
	{
		enum weapon weapon;
		unsigned char damage; // TODO double?
		unsigned char range;
	} ranged;
};

enum {UnitPeasant, UnitMilitia, UnitPikeman, UnitArcher, UnitLongbow, UnitLightCavalry, UnitBatteringRam};
enum {BuildingFarm, BuildingIrrigation, BuildingSawmill, BuildingMine, BuildingBlastFurnace, BuildingBarracks, BuildingArcheryRange, BuildingStables, BuildingWatchTower, BuildingPalisade, BuildingFortress, BuildingWorkshop, BuildingForge};

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

	struct region *location, *move;
};

struct region
{
	char name[NAME_LIMIT];
	size_t name_length;

	size_t index;
	struct region *neighbors[NEIGHBORS_LIMIT];
	struct polygon *location;
	struct point location_garrison;

	unsigned char owner;

	unsigned char train_progress;
	const struct unit *train[TRAIN_QUEUE];

	struct troop *troops;

	struct
	{
		// enum {East, NorthEast, North, NorthWest, West, SouthWest, South, SouthEast} position;

		unsigned char owner;
		struct troop *troops;

		unsigned siege;

		int assault;
	} garrison;

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
	enum armor armor_wall, armor_gate;
};

static inline const struct garrison_info *garrison_info(const struct region *restrict region)
{
	static const struct garrison_info info[] =
	{
		[PALISADE] = {.index = PALISADE, .troops = 3, .provisions = 2, .strength_wall = 160, .armor_wall = ARMOR_WOODEN, .strength_gate = 80, .armor_gate = ARMOR_WOODEN},
		[FORTRESS] = {.index = FORTRESS, .troops = 6, .provisions = 5, .strength_wall = 200, .armor_wall = ARMOR_STONE, .strength_gate = 120, .armor_gate = ARMOR_WOODEN},
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

int polygons_border(const struct polygon *restrict a, const struct polygon *restrict b, struct point *restrict first, struct point *restrict second);

void map_visible(const struct game *restrict game, unsigned char player, unsigned char visible[REGIONS_LIMIT]);

static inline int allies(const struct game *game, unsigned player0, unsigned player1)
{
	return (game->players[player0].alliance == game->players[player1].alliance);
}