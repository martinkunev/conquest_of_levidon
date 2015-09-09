#include "draw.h"
#include "image.h"
#include "interface.h"
#include "interface_common.h"

void show_flag(unsigned x, unsigned y, unsigned player)
{
	fill_rectangle(x + 4, y + 4, 24, 12, Player + player);
	image_draw(&image_flag, x, y);
}

void show_flag_small(unsigned x, unsigned y, unsigned player)
{
	fill_rectangle(x + 2, y + 2, 12, 6, Player + player);
	image_draw(&image_flag_small, x, y);
}

void show_button(const unsigned char *label, size_t label_size, unsigned x, unsigned y)
{
	struct box box = string_box(label, label_size, &font12);
	draw_string(label, label_size, x + (BUTTON_WIDTH - box.width) / 2, y + (BUTTON_HEIGHT - box.height) / 2, &font12, White);
	draw_rectangle(x - 1, y - 1, BUTTON_WIDTH + 2, BUTTON_HEIGHT + 2, White);
}
