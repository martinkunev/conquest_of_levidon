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

int input_map(const struct game *restrict game, unsigned char player);
int input_battle(const struct game *restrict game, unsigned char player);
