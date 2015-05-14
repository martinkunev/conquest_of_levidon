#include <stdlib.h>
#include <string.h>

#include "types.h"
#include "map.h"
#include "pathfinding.h"
#include "battle.h"
#include "movement.h"

#define heap_type struct troop *
#define heap_diff(a, b) ((a)->unit->speed >= (b)->unit->speed)
#include "heap.t"

int battlefield_init(const struct game *restrict game, struct battle *restrict battle, struct region *restrict region)
{
	struct troop **slots, *slot;
	struct pawn *pawns;
	size_t count;

	size_t i, j;

	// Initialize each field as empty.
	for(i = 0; i < BATTLEFIELD_HEIGHT; ++i)
	{
		for(j = 0; j < BATTLEFIELD_WIDTH; ++j)
		{
			battle->field[i][j].location = (struct point){j, i};
			battle->field[i][j].obstacle = OBSTACLE_NONE;
			battle->field[i][j].pawn = 0;
		}
	}

	count = 0;
	for(slot = region->troops; slot; slot = slot->_next)
		count += 1;
	pawns = malloc(count * sizeof(*pawns));
	if (!pawns) return -1;

	// Sort the slots by speed descending.
	slots = malloc(count * sizeof(*slots));
	if (!slots)
	{
		free(pawns);
		return -1;
	}
	struct heap heap = {.data = slots, .count = count};
	i = 0;
	for(slot = region->troops; slot; slot = slot->_next) slots[i++] = slot;
	heapify(&heap);
	while (--i)
	{
		slot = heap.data[0];
		heap_pop(&heap);
		slots[i] = slot;
	}

	memset(battle->player_pawns, 0, sizeof(battle->player_pawns));

	unsigned offset_defend = 0, offset_attack[NEIGHBORS_LIMIT] = {0};

	// Initialize a pawn for each slot.
	for(i = 0; i < count; ++i)
	{
		pawns[i].slot = slots[i];
		pawns[i].hurt = 0;

		pawns[i].fight = POINT_NONE;
		pawns[i].shoot = POINT_NONE;

		if (vector_add(battle->player_pawns + slots[i]->player, pawns + i) < 0)
		{
			free(slots);
			free(pawns);
			for(i = 0; i < game->players_count; ++i)
				free(battle->player_pawns[i].data);
			return -1;
		}

		unsigned column;
		if (pawns[i].slot->location == region)
		{
			column = offset_defend++;
		}
		else for(j = 0; j < NEIGHBORS_LIMIT; ++j)
		{
			if (pawns[i].slot->location == region->neighbors[j])
			{
				column = offset_attack[j]++;
				break;
			}
		}

		pawns[i].moves = malloc(32 * sizeof(*pawns[i].moves)); // TODO fix this

		// Put the pawns at their initial positions.
		const struct point *positions = formation_positions(pawns[i].slot, region);
		pawns[i].moves[0].location = positions[column];
		pawns[i].moves[0].time = 0.0;
		pawns[i].moves_count = 1;

		battle->field[positions[column].y][positions[column].x].pawn = pawns + i;
	}

	free(slots);

	battle->pawns = pawns;
	battle->pawns_count = count;
	return 0;
}

void battlefield_term(const struct game *restrict game, struct battle *restrict battle)
{
	size_t i;
	for(i = 0; i < game->players_count; ++i)
		free(battle->player_pawns[i].data);
	free(battle->pawns);
}

// Returns winner alliance number if the battle ended and -1 otherwise.
int battle_end(const struct game *restrict game, struct battle *restrict battle, unsigned char defender)
{
	int end = 1;
	int alive;

	signed char winner = -1;
	unsigned char alliance;

	struct pawn *pawn;

	size_t i, j;
	for(i = 0; i < game->players_count; ++i)
	{
		if (!battle->player_pawns[i].length) continue; // skip dead players

		alliance = game->players[i].alliance;

		alive = 0;
		for(j = 0; j < battle->player_pawns[i].length; ++j)
		{
			pawn = battle->player_pawns[i].data[j];
			if (pawn->slot->count)
			{
				alive = 1;

				if (winner < 0) winner = alliance;
				else if (alliance != winner) end = 0;
			}
		}

		// Mark players with no pawns left as dead.
		if (!alive) battle->player_pawns[i].length = 0;
	}

	if (end) return ((winner >= 0) ? winner : defender);
	else return -1;
}
