#define GLYPHS_COUNT 128

struct box
{
	unsigned width, height;
};

struct font
{
	GLuint textures[GLYPHS_COUNT];
	GLuint base;

	struct advance
	{
		unsigned char x, y;
	} advance[GLYPHS_COUNT];

	unsigned size;
};

extern struct font font10, font12, font24;

struct box string_box(const char *string, size_t length, struct font *restrict font);

void draw_cursor(const char *string, size_t length, unsigned x, unsigned y, struct font *restrict font, enum color color);
unsigned draw_string(const char *string, size_t length, unsigned x, unsigned y, struct font *restrict font, enum color color);

int font_init(void);
void font_term(void);
