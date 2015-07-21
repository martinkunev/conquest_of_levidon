struct state_report
{
	const struct game *game;
	const struct battle *battle;
};

int input_report(const struct game *restrict game, const struct battle *restrict battle);
