struct state_battle
{
	unsigned char player; // current player

	struct obstacles *obstacles; // obstacles on the battlefield
	struct adjacency_list *graph; // graph used for pathfinding

	struct point hover; // position of the hovered field

	unsigned char x, y; // position of the current field
	struct pawn *pawn; // selected pawn
};

struct state_formation
{
	unsigned char player; // current player

	struct point hover; // position of the hovered field

	struct pawn *pawn; // selected pawn
};

int input_formation(const struct game *restrict game, struct battle *restrict battle, unsigned char player);

int input_battle(const struct game *restrict game, struct battle *restrict battle, unsigned char player);
