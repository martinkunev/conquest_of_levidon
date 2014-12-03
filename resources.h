struct resources
{
	int gold;
	int food;
	int wood;
	int iron;
	int rock;
};

int resource_enough(const struct resources *restrict total, const struct resources *restrict required);
void resource_add(struct resources *restrict total, const struct resources *restrict change);
void resource_subtract(struct resources *restrict total, const struct resources *restrict change);
void resource_spend(struct resources *restrict total, const struct resources *restrict spent);

// TODO think how to prevent overflow
