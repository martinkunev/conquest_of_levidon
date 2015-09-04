void if_storage_init(const struct game *game, int width, int height);
void if_storage_term(void);

int if_storage_get(unsigned x, unsigned y);

void if_map(const void *argument, const struct game *game);
