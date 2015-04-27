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

// TODO fix these values
#define TOOLTIP_X 256
#define TOOLTIP_Y 730

enum object {Building};

// rows, columns, left, top, width, height, padding
#define OBJECT_GROUP(r, c, l, t, w, h, p) \
	{ \
		.rows = r, \
		.columns = c, \
		.left = l, \
		.right = l + w * c + p * (c - 1), \
		.top = t, \
		.bottom = t + h * r + p * (r - 1), \
		.width = w, \
		.height = h, \
		.width_padded = w + p, \
		.height_padded = h + p, \
		.padding = p, \
	}
static const struct object_group
{
	unsigned rows, columns;
	unsigned left, top;
	unsigned right, bottom;
	unsigned width, height;
	unsigned width_padded, height_padded;
	unsigned padding;
	unsigned count;
} object_group[] = {
	[Building] = OBJECT_GROUP(2, 5, PANEL_X + 1, PANEL_Y + 400, 48, 48, 1),
};
#undef OBJECT_GROUP

static inline struct point if_position(enum object object, unsigned index)
{
	struct point result;
	unsigned columns = object_group[object].columns;
	result.x = object_group[object].left + (index % columns) * object_group[object].width_padded;
	result.y = object_group[object].top + (index / columns) * object_group[object].height_padded;
	return result;
}

static inline int if_index(enum object object, struct point position)
{
	unsigned padding = object_group[object].padding;
	unsigned width_padded = object_group[object].width_padded;
	unsigned height_padded = object_group[object].height_padded;
	if (((position.x + padding) % width_padded) < padding) return -1; // horizontal padding
	if (((position.y + padding) % height_padded) < padding) return -1; // vertical padding
	return (object_group[object].columns * (position.y / height_padded) + (position.x / width_padded));
}

int if_init(void);

void if_map(const void *argument, const struct game *game);
void if_formation(const void *argument, const struct game *game);
void if_battle(const void *argument, const struct game *game);

void if_set(struct battlefield field[BATTLEFIELD_WIDTH][BATTLEFIELD_HEIGHT], struct battle *b);
void input_animation(const struct game *restrict game, const struct battle *restrict battle);
void if_regions_input(struct game *restrict game);
