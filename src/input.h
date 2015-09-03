enum
{
	INPUT_HANDLED = 0,	// event handled
	INPUT_NOTME,		// pass the event to the next handler
	INPUT_IGNORE,		// ignore event
	INPUT_FINISH,		// don't wait for more input
	INPUT_TERMINATE,	// user requests termination
};

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
