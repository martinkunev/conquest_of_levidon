#define MAP_WIDTH 16
#define MAP_HEIGHT 16

#define NEIGHBORS_MAX 8

struct player
{
	unsigned char alliance;
	//enum {Neutral, Local, Computer, Remote} type;
};

struct unit
{
	unsigned char health;
	unsigned char damage;
	unsigned char speed;
};

struct slot
{
	struct slot *_prev, *_next;
	const struct unit *unit;
	unsigned count;
	unsigned char player;

	struct region *location, *move;
};

struct region
{
	unsigned char owner;
	struct slot *slots;
	struct region *neighbors[NEIGHBORS_MAX];
	struct unit *train;
};

/* neighbors
listed in order: east, north-east, north, north-west, west, south-west, south, south-east
*/
