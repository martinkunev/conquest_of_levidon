#define PLAYER_NEUTRAL 0 /* player 0 is hard-coded as neutral */

int world_init(const union json *restrict json, struct game *restrict game);
void world_term(struct game *restrict game);
