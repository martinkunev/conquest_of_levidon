struct resources
{
	unsigned gold;
	unsigned food;
	unsigned wood;
	unsigned iron;
	unsigned rock;
};

int resource_enough(const struct resources *restrict total, const struct resources *restrict required);
void resource_collect(struct resources *restrict total, const struct resources *restrict collected);
void resource_spend(struct resources *restrict total, const struct resources *restrict spent);

// TODO think how to prevent overflow
