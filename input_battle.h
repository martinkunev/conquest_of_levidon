struct state_battle
{
	unsigned char player; // current player

	struct point hover; // position of the hovered field

	unsigned char x, y; // position of the current field
	struct pawn *pawn;

	int region;

	struct adjacency_list *nodes;
};

struct state_formation
{
	unsigned char player; // current player

	struct point hover; // position of the hovered field

	size_t region; // region index
	size_t pawn; // index of the selected pawn in player pawns
};

int input_formation(const struct game *restrict game, const struct region *restrict region, struct battle *restrict battle, unsigned char player);

int input_battle(const struct game *restrict game, struct battle *restrict battle, unsigned char player);
