struct state
{
	unsigned char player; // current player
	unsigned char x, y; // current field
	union
	{
		struct troop *slot;
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

	struct adjacency_list *nodes;

	struct point hover;
};

struct state_formation
{
	unsigned char player; // current player

	size_t region; // region index
	size_t pawn; // index of the selected pawn in player pawns

	struct point hover; // position of the hovered field
};

struct state_map
{
	unsigned char player; // current player

	ssize_t region; // index of the selected region

	struct troop *troop; // selected troop

	// TODO rename train
	enum {HOVER_NONE, HOVER_TROOP, HOVER_UNIT, HOVER_BUILDING, HOVER_TRAIN} hover_object; // type of the hovered object
	union
	{
		int troop;
		int unit;
		int building;
		int train;
	} hover; // index of the hovered object

	unsigned self_offset, ally_offset;
	unsigned self_count, ally_count;
};

extern struct state state;

enum {INPUT_NOTME = 1, INPUT_DONE, INPUT_TERMINATE};

#define EVENT_MOTION -127

struct area
{
	unsigned left, right, top, bottom;
	int (*callback)(int, unsigned, unsigned, uint16_t, const struct game *restrict);
};

int input_local(const struct area *restrict areas, size_t areas_count, void (*display)(const struct state *, const struct game *), const struct game *restrict game, struct state *state);
