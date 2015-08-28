struct files
{
	size_t count;
	struct bytes *names[];
};

#define DIRECTORIES_COUNT 3

int menu_init(void);
void menu_term(void);

struct files *menu_worlds(size_t index);
void menu_free(struct files *list);

int menu_load(size_t index, const unsigned char *restrict filename, size_t filename_size, struct game *restrict game);
int menu_save(size_t index, const unsigned char *restrict filename, size_t filename_size, const struct game *restrict game);
