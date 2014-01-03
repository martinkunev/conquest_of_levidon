void if_init(void);

void if_expose(const struct player *restrict players);

void if_set(struct pawn *bf[BATTLEFIELD_HEIGHT][BATTLEFIELD_WIDTH]);

int input_player(unsigned char player, const struct player *restrict players);

// TODO rename these
void if_regions(struct region reg[MAP_HEIGHT][MAP_WIDTH]);
int input_map(unsigned char player, const struct player *restrict players);
