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

// Returns whether the polygons share a border. Stores the border points in first and second (in the order of polygon a).
int polygons_border(const struct polygon *restrict a, const struct polygon *restrict b, struct point *restrict first, struct point *restrict second)
{
	size_t ai, bi;

	for(ai = 0; ai < a->vertices_count; ++ai)
	{
		for(bi = 0; bi < b->vertices_count; ++bi)
		{
			if (point_eq(a->points[ai], b->points[bi]))
			{
				size_t aj = (ai + 1) % a->vertices_count;
				size_t bj = (bi + b->vertices_count - 1) % b->vertices_count;
				if (point_eq(a->points[aj], b->points[bj]))
				{
					if (first) *first = a->points[ai];
					if (second) *second = a->points[aj];
					return 1;
				}
				else break; // a->points[ai] does not participate in a common border
			}
		}
	}

	return 0;
}

// TODO unify map_train and map_build in something like region_turn
// TODO implement something like region_stop that stops all trainings and constructions

void map_train(struct region *region)
{
	// Update training time and check if there are trained units.
	if (region->train[0] && (++region->train_progress == region->train[0]->time))
	{
		size_t i;

		if (troop_spawn(region, &region->troops, region->train[0], region->train[0]->troops_count, region->owner) < 0) abort(); // TODO

		region->train_progress = 0;
		for(i = 1; i < TRAIN_QUEUE; ++i)
			region->train[i - 1] = region->train[i];
		region->train[TRAIN_QUEUE - 1] = 0;
	}
}

void map_build(struct region *region)
{
	// Update construction time and check if the building is finished.
	if ((region->construct >= 0) && (++region->build_progress == buildings[region->construct].time))
	{
		region->built |= (1 << region->construct);
		region->construct = -1;
		region->build_progress = 0;
	}
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
