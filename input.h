struct state
{
	unsigned char player; // current player
	unsigned char x, y; // current field
	union
	{
		struct slot *slot;
		struct pawn *pawn;
	} selected;

	int region;
};

int input_map(const struct game *restrict game, unsigned char player);
int input_battle(const struct game *restrict game, unsigned char player);
