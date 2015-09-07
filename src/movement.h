struct pawn;

size_t movement_location(const struct pawn *restrict pawn, double time_now, double *restrict real_x, double *restrict real_y);

void pawn_place(struct pawn *restrict pawn, struct point location);

struct adjacency_list;
void movement_stay(struct pawn *restrict pawn);
int movement_queue(struct pawn *restrict pawn, struct point target, struct adjacency_list *restrict nodes, const struct obstacles *restrict obstacles);

int movement_attack(struct pawn *restrict pawn, struct point target, const struct battlefield field[BATTLEFIELD_HEIGHT][BATTLEFIELD_WIDTH], double reachable[BATTLEFIELD_HEIGHT][BATTLEFIELD_WIDTH], struct adjacency_list *restrict graph, const struct obstacles *restrict obstacles);
int movement_attack_plan(struct pawn *restrict pawn, struct adjacency_list *restrict graph, const struct obstacles *restrict obstacles);

void battlefield_movement_plan(const struct player *restrict players, size_t players_count, struct battlefield battlefield[][BATTLEFIELD_HEIGHT], struct pawn *restrict pawns, size_t pawns_count);
int battlefield_movement_perform(struct battle *restrict battle, struct pawn *restrict pawn, struct adjacency_list *restrict graph, const struct obstacles *restrict obstacles);
int battlefield_movement_attack(struct battle *restrict battle, struct pawn *restrict pawn, struct adjacency_list *restrict graph, const struct obstacles *restrict obstacles);

unsigned movement_visited(const struct pawn *restrict pawn, struct point visited[static UNIT_SPEED_LIMIT]);
