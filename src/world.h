union json;

int world_load(const unsigned char *restrict filepath, struct game *restrict game);
int world_save(const struct game *restrict game, const unsigned char *restrict filepath);
void world_unload(struct game *restrict game);
