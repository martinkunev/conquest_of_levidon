struct files
{
	size_t count;
	bytes_t *names[];
};

struct files *menu_worlds(const char *restrict path, size_t path_size);

int menu_load(const unsigned char *restrict location, size_t location_size, const unsigned char *restrict filename, size_t filename_size, struct game *restrict game);
int menu_save(const unsigned char *restrict location, size_t location_size, const unsigned char *restrict filename, size_t filename_size, const struct game *restrict game);
