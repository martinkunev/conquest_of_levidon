enum {INPUT_NOTME = 1, INPUT_DONE, INPUT_IGNORE, INPUT_TERMINATE};

#define EVENT_MOUSE_LEFT -1
#define EVENT_MOUSE_RIGHT -3
#define EVENT_MOTION -127

struct area
{
	unsigned left, right, top, bottom;
	int (*callback)(int, unsigned, unsigned, uint16_t, const struct game *restrict, void *);
	// TODO ? event mask
};

int input_local(const struct area *restrict areas, size_t areas_count, void (*display)(const void *, const struct game *), const struct game *restrict game, void *state);
