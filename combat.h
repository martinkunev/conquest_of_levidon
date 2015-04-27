int battlefield_shootable(const struct pawn *restrict pawn, struct point target);

void battlefield_fight(const struct game *restrict game, struct battle *restrict battle);
void battlefield_shoot(struct battle *battle);
void battlefield_clean_corpses(struct battle *battle);
