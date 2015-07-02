enum {REGION_NONE = -1};

struct state_map
{
	unsigned char player; // current player

	ssize_t region; // index of the selected region
	struct troop *troop; // selected troop

	enum {HOVER_NONE, HOVER_TROOP, HOVER_UNIT, HOVER_BUILDING, HOVER_DISMISS} hover_object; // type of the hovered object
	union
	{
		int troop;
		int unit;
		int building;
		int dismiss;
	} hover; // index of the hovered object

	unsigned self_offset, other_offset;
	unsigned self_count, other_count;

	unsigned char regions_visible[REGIONS_LIMIT];
};

int input_map(const struct game *restrict game, unsigned char player);
