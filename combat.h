int battlefield_fightable(const struct pawn *restrict pawn, const struct pawn *restrict target, const struct battle *restrict battle);
int battlefield_shootable(const struct pawn *restrict pawn, struct point target, const struct game *restrict game, const struct battle *restrict battle, const struct obstacles *restrict obstacles);

void battlefield_fight(const struct game *restrict game, struct battle *restrict battle);
void battlefield_shoot(struct battle *battle);
void battlefield_clean_corpses(struct battle *battle);
