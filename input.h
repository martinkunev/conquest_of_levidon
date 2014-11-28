struct state
{
	unsigned char player; // current player
	unsigned char x, y; // current field
	struct pawn *pawn; // TODO put this in the union
	union
	{
		struct slot *slot;
	} selected;

	int region;
};

int input_map(const struct game *restrict game, unsigned char player);
int input_battle(const struct game *restrict game, unsigned char player);
