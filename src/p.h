struct position
{
	float x, y;
};

int wall_blocks(struct position start, struct position end, float left, float right, float top, float bottom);
int pawn_blocks(struct position start, struct position end, struct position pawn);
