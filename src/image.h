struct image
{
	GLuint texture;
	uint32_t width, height;
};

int image_load_png(struct image *restrict image, const char *restrict filename, int grayscale);

void image_draw(const struct image *restrict image, unsigned x, unsigned y);
void display_image(const struct image *restrict image, unsigned x, unsigned y, unsigned width, unsigned height);

void image_unload(struct image *restrict image);
