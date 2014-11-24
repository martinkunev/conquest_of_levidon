void if_init(void);

void if_set(struct pawn *bf[BATTLEFIELD_HEIGHT][BATTLEFIELD_WIDTH]);

int input_player(unsigned char player, const struct player *restrict players);

// TODO rename these
void if_regions(struct region *restrict reg, size_t count, const struct unit *u, size_t u_count);
int input_map(unsigned char player, const struct player *restrict players);
