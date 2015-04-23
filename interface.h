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

#define FIELD_SIZE 32

#define MARGIN 4

#define SCROLL 8

#define SLOTS_VISIBLE 7

#define SLOT_X(x) (PANEL_X + SCROLL + 1 + (x) * (FIELD_SIZE + 1))
#define SLOT_Y(y) (PANEL_Y + 32 + MARGIN + 2 + 1 + (y) * (FIELD_SIZE + 18 + 2))

#define TRAIN_X(x) (PANEL_X + 80 + (x) * (FIELD_SIZE + 1))
#define TRAIN_Y (PANEL_Y + 300)

#define INVENTORY_X(x) (PANEL_X + 2 + 1 + (x) * (FIELD_SIZE + 1))
#define INVENTORY_Y (TRAIN_Y + FIELD_SIZE + MARGIN)

#define BUILDING_X(x) (PANEL_X + 1 + (x) * (FIELD_SIZE + 1))
#define BUILDING_Y (PANEL_Y + 400)

// TODO fix these values
#define TOOLTIP_X 256
#define TOOLTIP_Y 730

int if_init(void);

void if_set(struct battlefield field[BATTLEFIELD_WIDTH][BATTLEFIELD_HEIGHT], struct battle *b);

void if_map(const struct state *state, const struct game *game);
void if_formation(const struct state *state, const struct game *game);
void if_battle(const struct state *state, const struct game *game);

void input_animation(const struct game *restrict game, const struct battle *restrict battle);

void if_regions_input(struct game *restrict game);
