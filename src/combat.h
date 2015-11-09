double damage_expected(const struct pawn *restrict fighter, double troops_count, const struct pawn *restrict victim);
double damage_expected_ranged(const struct pawn *restrict shooter, double troops_count, const struct pawn *restrict victim);

void battle_fight(const struct game *restrict game, struct battle *restrict battle);
void battle_shoot(struct battle *battle, const struct obstacles *restrict obstacles);
int battlefield_clean(struct battle *battle);

int combat_order_fight(const struct game *restrict game, const struct battle *restrict battle, const struct obstacles *restrict obstacles, struct pawn *restrict fighter, struct pawn *restrict victim);
int combat_order_assault(const struct game *restrict game, const struct battle *restrict battle, const struct obstacles *restrict obstacles, struct pawn *restrict fighter, struct point target);
int combat_order_shoot(const struct game *restrict game, const struct battle *restrict battle, const struct obstacles *restrict obstacles, struct pawn *restrict shooter, struct point target);

extern const double damage_boost[7][6];
