#define PLAYER_NEUTRAL 0 /* player 0 is hard-coded as neutral */

int world_load(const union json *restrict json, struct game *restrict game);
union json *world_save(const struct game *restrict game);
void world_unload(struct game *restrict game);