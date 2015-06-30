//#define SCREEN_WIDTH 1024
//#define SCREEN_HEIGHT 768

#define SCREEN_WIDTH 1920
#define SCREEN_HEIGHT 1080

#define MAP_WIDTH 768
#define MAP_HEIGHT 768
#define MAP_X 256
#define MAP_Y 0

#define BATTLE_X 8
#define BATTLE_Y 8

#define CTRL_X 768
#define CTRL_Y 0
#define CTRL_WIDTH 256
#define CTRL_HEIGHT 768
#define CTRL_MARGIN 32

#define PANEL_X 4
#define PANEL_Y 4
#define PANEL_WIDTH 248
#define PANEL_HEIGHT 760

// TODO fix these values
#define TOOLTIP_X 256
#define TOOLTIP_Y 730

#define FIELD_SIZE 30

#define MARGIN 4

#define SCROLL 8

#define TROOPS_VISIBLE 7
#define TROOPS_GARRISON 6

#define GARRISON_X (PANEL_X + 1)
#define GARRISON_Y (PANEL_Y + 160)
#define GARRISON_MARGIN 24

enum object {Building, Inventory, Dismiss, TroopSelf, TroopOther, TroopGarrison, Battlefield};

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
		.span_x = w * c + p * (c - 1), \
		.span_y = h * r + p * (r - 1), \
		.padding = p, \
	}
static const struct object_group
{
	unsigned rows, columns;
	unsigned left, top;
	unsigned right, bottom;
	unsigned width, height;
	unsigned width_padded, height_padded;
	unsigned span_x, span_y;
	unsigned padding;
	unsigned count;
} object_group[] = {
	[Building] = OBJECT_GROUP(3, 5, PANEL_X + 1, PANEL_Y + 400, 48, 48, 1),
	[Inventory] = OBJECT_GROUP(1, 5, PANEL_X + 1, PANEL_Y + 340, 32, 32, 1),
	[Dismiss] = OBJECT_GROUP(1, TRAIN_QUEUE, PANEL_X + 81, PANEL_Y + 300, 32, 32, 1),
	[TroopSelf] = OBJECT_GROUP(1, TROOPS_VISIBLE, PANEL_X + SCROLL + 1, PANEL_Y + 36 + 2, 32, 32, 1),
	[TroopOther] = OBJECT_GROUP(1, TROOPS_VISIBLE, PANEL_X + SCROLL + 1, PANEL_Y + 36 + 48 + 2 + 2, 32, 32, 1),
	[TroopGarrison] = OBJECT_GROUP(1, TROOPS_GARRISON, GARRISON_X + 9, GARRISON_Y + GARRISON_MARGIN, 32, 32, 1),
	[Battlefield] = OBJECT_GROUP(BATTLEFIELD_HEIGHT, BATTLEFIELD_WIDTH, BATTLE_X, BATTLE_Y, FIELD_SIZE, FIELD_SIZE, 0),
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

int if_init(const struct game *game);

int if_storage_get(unsigned x, unsigned y);

void if_map(const void *argument, const struct game *game);
void if_formation(const void *argument, const struct game *game);
void if_battle(const void *argument, const struct game *game);

void if_set(struct battle *b);
void input_animation(const struct game *restrict game, const struct battle *restrict battle);
void if_regions_input(struct game *restrict game);
