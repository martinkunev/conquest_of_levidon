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

#define PANEL_X 4
#define PANEL_Y 4

#define PANEL_WIDTH 248
#define PANEL_HEIGHT 760

#define PAWN_MARGIN 4

#define FIELD_SIZE 32

//

#define MARGIN 4

#define SLOT_X(x) (PANEL_X + 2 + 1 + (x) * (FIELD_SIZE + 3))
#define SLOT_Y(y) (PANEL_Y + 32 + MARGIN + 2 + 1 + (y) * (FIELD_SIZE + 18 + 2))

#define TRAIN_X(x) (PANEL_X + 100 + (x) * (FIELD_SIZE + 1))
#define TRAIN_Y (PANEL_Y + 200)

#define INVENTORY_X(x) (PANEL_X + (x) * (FIELD_SIZE + 1))
#define INVENTORY_Y (TRAIN_Y + FIELD_SIZE + MARGIN)

void if_init(void);

void if_set(struct pawn *bf[BATTLEFIELD_HEIGHT][BATTLEFIELD_WIDTH]);

void if_map(const struct player *restrict players, const struct state *restrict state);
void if_battle(const struct player *restrict players, const struct state *restrict state);

int if_battle_animation(void);

void if_regions(struct game *restrict game);
