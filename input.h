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

int input_map(unsigned char player, const struct player *restrict players);
int input_battle(unsigned char player, const struct player *restrict players);
