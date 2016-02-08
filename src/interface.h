extern struct font font9, font12, font24;
extern unsigned SCREEN_WIDTH, SCREEN_HEIGHT;

struct game;

int if_init(void);
void if_display(void);
void input_display(void (*if_display)(const void *, const struct game *), const struct game *restrict game, void *state);
void if_term(void);
