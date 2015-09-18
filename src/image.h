struct image
{
	GLuint texture;
	uint32_t width, height;
};

int image_load_png(struct image *restrict image, const char *restrict filename, int grayscale);

void image_draw(const struct image *restrict image, unsigned x, unsigned y);
void display_image(const struct image *restrict image, unsigned x, unsigned y, unsigned width, unsigned height);

void image_unload(struct image *restrict image);

extern struct image image_selected, image_assault, image_flag, image_flag_small, image_panel, image_construction, image_movement;
extern struct image image_pawn_fight, image_pawn_assault, image_pawn_shoot;
extern struct image image_shoot_right, image_shoot_up, image_shoot_left, image_shoot_down;
extern struct image image_terrain[1];
extern struct image image_garrison[2];
extern struct image image_map_village, image_map_garrison[2];
extern struct image image_gold, image_food, image_wood, image_stone, image_iron, image_time;
extern struct image image_scroll_left, image_scroll_right;
extern struct image image_units[7];
extern struct image image_buildings[13];
extern struct image image_buildings_gray[13];
extern struct image image_palisade[16], image_palisade_gate[2], image_fortress[16], image_fortress_gate[2];
