struct state
{
	unsigned char player; // current player
	unsigned char x, y; // current field
	union
	{
		struct slot *slot;
		struct pawn *pawn;
	} selected;
	struct
	{
		signed char building;
		signed char unit;
	} pointed;

	unsigned self_offset, ally_offset;
	unsigned self_count, ally_count;

	int region;
};

enum {INPUT_NOTME = 1, INPUT_DONE, INPUT_TERMINATE};

#define EVENT_MOTION -127

struct area
{
	unsigned left, right, top, bottom;
	int (*callback)(int, unsigned, unsigned, uint16_t, const struct game *restrict);
};

int input_local(void (*display)(const struct player *restrict, const struct state *restrict, const struct game *restrict), const struct area *restrict areas, size_t areas_count, const struct game *restrict game);

int input_map(const struct game *restrict game, unsigned char player);
int input_battle(const struct game *restrict game, const struct battle *restrict battle, unsigned char player);

extern struct state state;
