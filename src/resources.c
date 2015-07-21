#include "resources.h"

// Check whether there are enough resources available for a given requirement.
int resource_enough(const struct resources *restrict total, const struct resources *restrict required)
{
	if (total->gold < required->gold) return 0;
	if (total->food < required->food) return 0;
	if (total->wood < required->wood) return 0;
	if (total->iron < required->iron) return 0;
	if (total->stone < required->stone) return 0;
	return 1;
}

// Add resource change to the total amount of resources.
void resource_add(struct resources *restrict total, const struct resources *restrict change)
{
	total->gold += change->gold;
	total->food += change->food;
	total->wood += change->wood;
	total->iron += change->iron;
	total->stone += change->stone;
}

// Return spent resources to the total amount of resources.
void resource_subtract(struct resources *restrict total, const struct resources *restrict change)
{
	total->gold -= change->gold;
	total->food -= change->food;
	total->wood -= change->wood;
	total->iron -= change->iron;
	total->stone -= change->stone;
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
	if (total->stone < 0) total->stone = 0;
}

// Multiply resource by factor and store the result in total.
void resource_multiply(struct resources *restrict total, const struct resources *restrict resource, unsigned factor)
{
	total->gold = resource->gold * factor;
	total->food = resource->food * factor;
	total->wood = resource->wood * factor;
	total->iron = resource->iron * factor;
	total->stone = resource->stone * factor;
}