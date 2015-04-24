size_t movement_location(const struct pawn *restrict pawn, double time_now, double *restrict real_x, double *restrict real_y);

struct adjacency_list;
int movement_set(struct pawn *restrict pawn, struct point target, struct adjacency_list *restrict nodes);
int movement_queue(struct pawn *restrict pawn, struct point target, struct adjacency_list *restrict nodes);

void movement_stay(struct pawn *restrict pawn);
