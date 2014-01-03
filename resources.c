#include "resources.h"

// Check whether there are enough resources available for a given requirement.
int resource_enough(const struct resources *restrict total, const struct resources *restrict required)
{
	if (required->gold > total->gold) return 0;
	if (required->food > total->food) return 0;
	if (required->wood > total->wood) return 0;
	if (required->iron > total->iron) return 0;
	if (required->rock > total->rock) return 0;
	return 1;
}

// Add the collected resources to the total amount of resources.
void resource_collect(struct resources *restrict total, const struct resources *restrict collected)
{
	total->gold += collected->gold;
	total->food += collected->food;
	total->wood += collected->wood;
	total->iron += collected->iron;
	total->rock += collected->rock;
}

// Substract the spent resoures from the total amount.
// If more than the total of a given resource is spent, set the total amount to 0.
void resource_spend(struct resources *restrict total, const struct resources *restrict spent)
{
	total->gold = ((total->gold > spent->gold) ? (total->gold - spent->gold) : 0);
	total->food = ((total->food > spent->food) ? (total->food - spent->food) : 0);
	total->wood = ((total->wood > spent->wood) ? (total->wood - spent->wood) : 0);
	total->iron = ((total->iron > spent->iron) ? (total->iron - spent->iron) : 0);
	total->rock = ((total->rock > spent->rock) ? (total->rock - spent->rock) : 0);
}
