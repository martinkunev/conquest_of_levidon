int computer_formation(const struct game *restrict game, struct battle *restrict battle, unsigned char player);
int computer_battle(const struct game *restrict game, struct battle *restrict battle, unsigned char player, struct adjacency_list *restrict graph, const struct obstacles *restrict obstacles);
