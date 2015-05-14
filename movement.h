struct pawn;

size_t movement_location(const struct pawn *restrict pawn, double time_now, double *restrict real_x, double *restrict real_y);

struct adjacency_list;
int movement_set(struct pawn *restrict pawn, struct point target, struct adjacency_list *restrict nodes);
int movement_queue(struct pawn *restrict pawn, struct point target, struct adjacency_list *restrict nodes);

void movement_stay(struct pawn *restrict pawn);

int battlefield_movement_plan(const struct player *restrict players, size_t players_count, struct battlefield battlefield[][BATTLEFIELD_HEIGHT], struct pawn *restrict pawns, size_t pawns_count);
void battlefield_movement_perform(struct battlefield battlefield[][BATTLEFIELD_HEIGHT], struct pawn *restrict pawns, size_t pawns_count);
