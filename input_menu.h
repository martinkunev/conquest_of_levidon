#define FILENAME_LIMIT 64

struct state
{
	struct files *worlds;
	ssize_t world;

	size_t directory;

	char filename[FILENAME_LIMIT];
	size_t filename_size;
};

int input_load(struct game *restrict game);
int input_save(const struct game *restrict game);
