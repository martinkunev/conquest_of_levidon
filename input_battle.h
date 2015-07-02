struct state_battle
{
	unsigned char player; // current player

	struct point hover; // position of the hovered field

	struct obstacles *obstacles; // obstacles on the battlefield
	struct adjacency_list *graph; // graph used for pathfinding

	struct point field; // selected field
	struct pawn *pawn; // selected pawn

	double reachable[BATTLEFIELD_HEIGHT][BATTLEFIELD_WIDTH];
};

struct state_formation
{
	unsigned char player; // current player

	struct point hover; // position of the hovered field

	struct pawn *pawn; // selected pawn

	struct point reachable[REACHABLE_LIMIT];
	size_t reachable_count;
};

int input_formation(const struct game *restrict game, struct battle *restrict battle, unsigned char player);

int input_battle(const struct game *restrict game, struct battle *restrict battle, unsigned char player);
