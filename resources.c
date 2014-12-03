#include "resources.h"

// Check whether there are enough resources available for a given requirement.
int resource_enough(const struct resources *restrict total, const struct resources *restrict required)
{
	if (total->gold < required->gold) return 0;
	if (total->food < required->food) return 0;
	if (total->wood < required->wood) return 0;
	if (total->iron < required->iron) return 0;
	if (total->rock < required->rock) return 0;
	return 1;
}

// Add resource change to the total amount of resources.
void resource_add(struct resources *restrict total, const struct resources *restrict change)
{
	total->gold += change->gold;
	total->food += change->food;
	total->wood += change->wood;
	total->iron += change->iron;
	total->rock += change->rock;
}

// Return spent resources to the total amount of resources.
void resource_subtract(struct resources *restrict total, const struct resources *restrict change)
{
	total->gold -= change->gold;
	total->food -= change->food;
	total->wood -= change->wood;
	total->iron -= change->iron;
	total->rock -= change->rock;
}

// Substract the spent resoures from the total amount.
// If more than the total of a given resource is spent, set the total amount to 0.
void resource_spend(struct resources *restrict total, const struct resources *restrict spent)
{
	resource_subtract(total, spent);
	if (total->gold < 0) total->gold = 0;
	if (total->food < 0) total->food = 0;
	if (total->wood < 0) total->wood = 0;
	if (total->iron < 0) total->iron = 0;
	if (total->rock < 0) total->rock = 0;
}
