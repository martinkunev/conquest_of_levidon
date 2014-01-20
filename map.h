#define MAP_WIDTH 16
#define MAP_HEIGHT 16

#define NEIGHBORS_MAX 8
#define TRAIN_QUEUE 4

#include "resources.h"

struct player
{
	enum {Neutral, Local, Computer, Remote} type;
	struct resources treasury;
	unsigned char alliance;
};

struct unit
{
	unsigned char health;
	unsigned char damage;
	unsigned char speed;

	struct resources cost;
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
	struct region *neighbors[NEIGHBORS_MAX];
	struct unit *train[TRAIN_QUEUE];
	struct resources income;

	struct polygon
	{
		size_t count;
		struct point 
		{ 
			unsigned x, y;
		} points[];
	} location;
	size_t index;

	// TODO store position in a better way
	unsigned char x, y;
};

/* neighbors
listed in order: east, north-east, north, north-west, west, south-west, south, south-east
*/

extern struct unit peasant;
