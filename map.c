#include <stdlib.h>
#include <string.h>

#include "map.h"

void region_income(const struct region* restrict region, struct resources *restrict income)
{
	size_t i;

	income->gold += 1;
	income->food += 1;

	for(i = 0; i < buildings_count; ++i)
		if (region->built & (1 << i))
			resource_add(income, &buildings[i].income);
}

void troop_attach(struct troop **troops, struct troop *troop)
{
	troop->_prev = 0;
	troop->_next = *troops;
	if (*troops) (*troops)->_prev = troop;
	*troops = troop;
}

void troop_detach(struct troop **troops, struct troop *troop)
{
	if (troop->_prev) troop->_prev->_next = troop->_next;
	else *troops = troop->_next;
	if (troop->_next) troop->_next->_prev = troop->_prev;
}

void troop_remove(struct troop **troops, struct troop *troop)
{
	troop_detach(troops, troop);
	free(troop);
}

int troop_spawn(struct region *restrict region, struct troop **restrict troops, const struct unit *restrict unit, unsigned count, unsigned char owner)
{
	struct troop *troop = malloc(sizeof(*troop));
	if (!troop) return -1;

	troop->unit = unit;
	troop->count = count;
	troop->owner = owner;

	troop->_prev = 0;
	troop->_next = *troops;
	if (*troops) (*troops)->_prev = troop;
	*troops = troop;
	troop->move = troop->location = region;

	return 0;
}

// Determine which regions are visible for the current player.
void map_visible(const struct game *restrict game, unsigned char player, unsigned char visible[REGIONS_LIMIT])
{
	size_t i, j;

	memset(visible, 0, REGIONS_LIMIT);

	for(i = 0; i < game->regions_count; ++i)
	{
		const struct region *restrict region = game->regions + i;

		if (allies(game, player, region->owner))
		{
			visible[i] = 1;

			// Make the neighboring regions visible when a watch tower is built.
			if (region_built(region, BuildingWatchTower))
			{
				for(j = 0; j < NEIGHBORS_LIMIT; ++j)
				{
					struct region *neighbor = region->neighbors[j];
					if (neighbor) visible[neighbor->index] = 1;
				}
			}
		}
		else if (allies(game, player, region->garrison.owner)) visible[i] = 1;
	}
}
