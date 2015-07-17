#define FILENAME_LIMIT 64

struct state
{
	struct files *worlds;
	ssize_t world;

	size_t directory;

	char name[FILENAME_LIMIT];
	size_t name_size;
	size_t name_position;
};

int input_load(struct game *restrict game);
int input_save(const struct game *restrict game);
