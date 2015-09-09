struct state_report
{
	const struct game *game;
	const struct battle *battle;
};

int input_report_battle(const struct game *restrict game, const struct battle *restrict battle);
int input_report_map(const struct game *restrict game);
