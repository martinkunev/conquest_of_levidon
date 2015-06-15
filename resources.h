struct resources
{
	int gold;
	int food;
	int wood;
	int iron;
	int stone;
};

int resource_enough(const struct resources *restrict total, const struct resources *restrict required);
void resource_add(struct resources *restrict total, const struct resources *restrict change);
void resource_subtract(struct resources *restrict total, const struct resources *restrict change);
void resource_spend(struct resources *restrict total, const struct resources *restrict spent);
void resource_multiply(struct resources *restrict total, const struct resources *restrict resource, unsigned factor);

// TODO think how to prevent overflow
