struct image
{
	GLuint texture;
	uint32_t width, height;
};

int image_load_png(struct image *restrict image, const char *restrict filename);
void image_draw(const struct image *restrict image, unsigned x, unsigned y);
void image_unload(struct image *restrict image);
