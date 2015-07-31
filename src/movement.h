struct pawn;

size_t movement_location(const struct pawn *restrict pawn, double time_now, double *restrict real_x, double *restrict real_y);

struct adjacency_list;
int movement_set(struct pawn *restrict pawn, struct point target, struct adjacency_list *restrict nodes, const struct obstacles *restrict obstacles);
int movement_queue(struct pawn *restrict pawn, struct point target, struct adjacency_list *restrict nodes, const struct obstacles *restrict obstacles);
int movement_follow(struct pawn *restrict pawn, const struct pawn *restrict target, struct adjacency_list *restrict graph, const struct obstacles *restrict obstacles);

void movement_stay(struct pawn *restrict pawn);

int movement_attack(struct pawn *restrict pawn, struct point target, const struct battlefield field[BATTLEFIELD_HEIGHT][BATTLEFIELD_WIDTH], double reachable[BATTLEFIELD_HEIGHT][BATTLEFIELD_WIDTH], struct adjacency_list *restrict graph, const struct obstacles *restrict obstacles);

void battlefield_movement_plan(const struct player *restrict players, size_t players_count, struct battlefield battlefield[][BATTLEFIELD_HEIGHT], struct pawn *restrict pawns, size_t pawns_count);
void battlefield_movement_perform(struct battlefield battlefield[][BATTLEFIELD_HEIGHT], struct pawn *restrict pawns, size_t pawns_count);
