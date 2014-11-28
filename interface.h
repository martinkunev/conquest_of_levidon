#define SCREEN_WIDTH 1024
#define SCREEN_HEIGHT 768

#define MAP_WIDTH 768
#define MAP_HEIGHT 768
#define MAP_X 256
#define MAP_Y 0

#define BATTLE_X 0
#define BATTLE_Y 0

#define CTRL_X 768
#define CTRL_Y 32

#define PANEL_X 0
#define PANEL_Y 32

#define PANEL_WIDTH 248
#define PANEL_HEIGHT 760

#define PAWN_MARGIN 4

#define FIELD_SIZE 32

void if_init(void);

void if_set(struct pawn *bf[BATTLEFIELD_HEIGHT][BATTLEFIELD_WIDTH]);

void if_map(const struct player *restrict players, const struct state *restrict state);
void if_battle(const struct player *restrict players, const struct state *restrict state);

void if_regions(struct region *restrict reg, size_t count, const struct unit *u, size_t u_count);
