struct point 
{ 
	unsigned x, y;
};

struct polygon
{
	size_t vertices;
	struct point points[];
};

enum color {White, Gray, Black, B0, Progress, Select, Self, Ally, Enemy, Player};
extern unsigned char display_colors[][4]; // TODO remove this

void display_rectangle(unsigned x, unsigned y, unsigned width, unsigned height, enum color color);
void display_polygon(const struct polygon *restrict polygon, int offset_x, int offset_y);
