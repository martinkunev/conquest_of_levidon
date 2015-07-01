void battlefield_fight(const struct game *restrict game, struct battle *restrict battle);
void battlefield_shoot(struct battle *battle);
void battlefield_clean(struct battle *battle);

int combat_fight(const struct game *restrict game, const struct battle *restrict battle, const struct obstacles *restrict obstacles, struct pawn *restrict fighter, struct pawn *restrict victim);
int combat_assault(const struct game *restrict game, const struct battle *restrict battle, const struct obstacles *restrict obstacles, struct pawn *restrict fighter, struct point target);
int combat_shoot(const struct game *restrict game, const struct battle *restrict battle, const struct obstacles *restrict obstacles, struct pawn *restrict shooter, struct point target);
