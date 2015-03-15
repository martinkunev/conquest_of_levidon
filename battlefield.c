#include <stdlib.h>
#include <string.h>

#include "types.h"
#include "json.h"
#include "map.h"
#include "battlefield.h"

#define BATTLEFIELD_WIDTH 24
#define BATTLEFIELD_HEIGHT 24

#define MOVEMENT_STEPS (UNIT_SPEED_LIMIT * 2)

// Due to pawn dimensions, at most 4 can try to move to a given square at the same time.
#define OVERLAP_LIMIT 4

#define heap_type struct slot *
#define heap_diff(a, b) ((a)->unit->speed >= (b)->unit->speed)
#define heap_update(heap, position)
#include "heap.t"
#undef heap_update
#undef heap_diff
#undef heap_type

/*
Each field in the battlefield is divided into four squares. Each pawn occupies four squares.
The movement is divided into MOVEMENT_STEPS steps (ensuring that each pawn moves at most one square per step).
For each pawn a failback field is chosen for each step. No two pawns can have the same failback field at a given step.
When two pawns try to occupy the same square at a given step, their movement is changed to avoid that.

When enemy pawns try to occupy the same square, they are redirected to their failback locations.
When allied pawns try to occupy the same squre, one of them stays where it is and the other are detoured to their failback locations to wait.
*/

static struct queue_item *pawn_location(const struct queue *restrict moves, double time_now, struct point *restrict location)
{
	struct queue_item *item;

	double time_start, time_end;
	double progress; // progress of the current move; 0 == start point; 1 == end point

	double real_x, real_y;

	for(item = moves->first; item->next; item = item->next)
	{
		time_start = item->data.time;
		if (time_now < time_start) break; // this move has not started yet
		time_end = item->next->data.time;
		if (time_now > time_end) continue; // this move is already done

		progress = (time_now - time_start) / (time_end - time_start);

		real_x = item->next->data.location.x * progress + item->next->data.location.x * (1 - progress);
		real_y = item->next->data.location.y * progress + item->next->data.location.y * (1 - progress);

		location->x = real_x * 2;
		location->y = real_y * 2;

		return item;
	}

	return 0; // the pawn is immobile
}

static inline void square_occupy(struct pawn **occupied, struct pawn *restrict pawn, int failback)
{
	size_t i = 0;
	while (occupied[i]) i += 1;
	if (failback && i)
	{
		occupied[i] = occupied[0];
		occupied[0] = pawn;
	}
	else occupied[i] = pawn;
}

static inline void square_clear(struct pawn **occupied, const struct pawn *restrict pawn)
{
	size_t i, last = OVERLAP_LIMIT - 1;
	while (!occupied[last]) last -= 1;
	for(i = last; occupied[i] != pawn; --i) ;
	if (i != last)
	{
		occupied[i] = occupied[last];
		occupied[last] = 0;
	}
}

static void pawn_detour(struct pawn *occupied[BATTLEFIELD_HEIGHT][BATTLEFIELD_WIDTH][OVERLAP_LIMIT], struct pawn *pawn, unsigned x, unsigned y)
{
	if ((pawn->step.x != x) && (pawn->step.y != y)) square_clear(occupied[y][x], pawn);
	if ((pawn->step.x + 1 != x) && (pawn->step.y != y)) square_clear(occupied[y][x + 1], pawn);
	if ((pawn->step.x != x) && (pawn->step.y + 1 != y)) square_clear(occupied[y + 1][x], pawn);
	if ((pawn->step.x + 1 != x) && (pawn->step.y + 1 != y)) square_clear(occupied[y + 1][x + 1], pawn);

	pawn->step = pawn->failback;

	if ((pawn->step.x != x) && (pawn->step.y != y)) square_occupy(occupied[y][x], pawn, 1);
	if ((pawn->step.x + 1 != x) && (pawn->step.y != y)) square_occupy(occupied[y][x + 1], pawn, 1);
	if ((pawn->step.x != x) && (pawn->step.y + 1 != y)) square_occupy(occupied[y + 1][x], pawn, 1);
	if ((pawn->step.x + 1 != x) && (pawn->step.y + 1 != y)) square_occupy(occupied[y + 1][x + 1], pawn, 1);
}

void moves_free(struct queue_item *item)
{
	struct queue_item *next;
	while (item)
	{
		next = item->next;
		free(item);
		item = next;
	}
}

static int pawn_wait(struct pawn *occupied[BATTLEFIELD_HEIGHT][BATTLEFIELD_WIDTH][OVERLAP_LIMIT], struct pawn *pawn, unsigned step)
{
	// Collisions can only occur after the start of the movement.
	// assert(step);

	// Make the pawn detour to its failback at the previous step and continue moving toward its next location the step after.

	double time_detour = ((double)step - 1) / MOVEMENT_STEPS;
	double distance;

	struct queue_item *current, *next;

	if (time_detour > pawn->moves.first->data.time)
	{
		// The pawn has started moving at the time of the detour.

		struct move move;

		struct point location;
		current = pawn_location(&pawn->moves, time_detour, &location);

		// assert(current);
		// assert(current->next);

		if (queue_insert(&pawn->moves, current, move) < 0) return -1;
		next = current->next;

		next->data.location = location;
		next->data.distance = current->data.distance + battlefield_distance(current->data.location, next->data.location);
		next->data.time = time_detour;

		if (!point_eq(location, pawn->failback))
		{
			current = next;
			if (queue_insert(&pawn->moves, current, move) < 0) return -1;
			next = current->next;

			// Make the failback location the pawn's next location.
			next->data.location = pawn->failback;
			next->data.distance = current->data.distance + battlefield_distance(current->data.location, next->data.location);
			next->data.time = (double)step / MOVEMENT_STEPS;
		}

		// Update time and distance for the next movements.
		current = current->next;
		while (next = current->next)
		{
			distance = battlefield_distance(current->data.location, next->data.location);
			next->data.distance = current->data.distance + distance;
			next->data.time = current->data.time + distance / pawn->slot->unit->speed;
			current = next;
		}
	}
	else
	{
		// The pawn is at its failback location at the time of the detour.

		double wait = ((double)step / MOVEMENT_STEPS) - pawn->moves.first->data.time;

		current = pawn->moves.first;
		while (current)
		{
			current->data.time += wait;
			current = current->next;
		}
	}

	return 0;
}

static int pawn_stop(struct pawn *occupied[BATTLEFIELD_HEIGHT][BATTLEFIELD_WIDTH][OVERLAP_LIMIT], struct pawn *pawn, unsigned step)
{
	// Collisions can only occur after the start of the movement.
	// assert(step);

	// Make the pawn detour to its failback at the previous step.

	double time_detour = ((double)step - 1) / MOVEMENT_STEPS;

	struct queue_item *current, *next;

	if (time_detour > pawn->moves.first->data.time)
	{
		// The pawn has started moving at the time of the detour.

		struct point location;
		current = pawn_location(&pawn->moves, time_detour, &location);

		// assert(current);
		// assert(current->next);

		next = current->next;

		next->data.location = location;
		next->data.distance = current->data.distance + battlefield_distance(current->data.location, next->data.location);
		next->data.time = time_detour;

		if (!point_eq(location, pawn->failback))
		{
			current = next;
			if (!current->next)
			{
				struct move move;
				if (queue_push(&pawn->moves, move) < 0) return -1;
			}
			next = current->next;

			// Make the failback location the pawn's next location.
			next->data.location = pawn->failback;
			next->data.distance = current->data.distance + battlefield_distance(current->data.location, next->data.location);
			next->data.time = (double)step / MOVEMENT_STEPS;
		}

		// Cancel moves after the failback point.
		moves_free(next->next);
		next->next = 0;
	}
	else
	{
		// The pawn is at its failback location at the time of the detour.

		// Cancel all the moves.
		moves_free(pawn->moves.first->next);
		pawn->moves.first->next = 0;
	}

	return 0;
}

int battlefield_movement_plan(const struct player *restrict players, size_t players_count, struct battlefield battlefield[][BATTLEFIELD_HEIGHT], struct pawn *restrict pawns, size_t pawns_count)
{
	// TODO move times must be set before this function is called
	// TODO pawns must be ordered in decending order by speed

	unsigned step;
	double now;

	size_t p;
	struct pawn *pawn;

	// Store a list of the pawns occupying a given square.
	// Pawn at its failback location is placed first in the list.
	struct pawn *occupied[BATTLEFIELD_HEIGHT][BATTLEFIELD_WIDTH][OVERLAP_LIMIT];
	struct pawn **square;

	size_t x, y;
	size_t i;

	unsigned char alliance;
	unsigned char speed;
	unsigned char fastest_speed, fastest_count, fastest_offset;

	int failback;
	int changed;

	for(p = 0; p < pawns_count; ++p)
		pawns[p].failback = pawns[p].moves.first->data.location;

	for(step = 0; step < MOVEMENT_STEPS; ++step)
	{
		now = (double)step / MOVEMENT_STEPS;

		// Update occupied squares for each pawn.
		for(p = 0; p < pawns_count; ++p)
		{
			pawn = pawns + p;
			pawn_location(&pawn->moves, now, &pawn->step);

			failback = point_eq(pawn->step, pawn->failback);
			square_occupy(occupied[pawn->step.y][pawn->step.x], pawn, failback);
			square_occupy(occupied[pawn->step.y][pawn->step.x + 1], pawn, failback);
			square_occupy(occupied[pawn->step.y + 1][pawn->step.x], pawn, failback);
			square_occupy(occupied[pawn->step.y + 1][pawn->step.x + 1], pawn, failback);
		}

		// Revert the location of overlapping pawns to their failback.
		// If the overlapping pawns are enemies, make them stay and fight.
		// If the overlapping pawns are allies, make the slower pawns wait the faster pass.
		// Repeat as long as pawn movement changed during the last pass.
		// Each repetition causes at least one pawn to move to its failback. This ensures the algorithm will finish.
		do
		{
			changed = 0;

			for(y = 0; y < BATTLEFIELD_HEIGHT; ++y)
			{
				for(x = 0; x < BATTLEFIELD_WIDTH; ++x)
				{
					square = occupied[y][x];
					if (square[1]) // movement overlap
					{
						changed = 1;

						alliance = players[square[0]->slot->player].alliance;
						fastest_speed = square[0]->slot->unit->speed;
						fastest_count = 1;

						for(i = 1; (i < OVERLAP_LIMIT) && square[i]; ++i)
						{
							if (players[square[i]->slot->player].alliance != alliance) goto ready;

							speed = square[i]->slot->unit->speed;
							if (speed > fastest_speed)
							{
								fastest_speed = speed;
								fastest_count = 1;
							}
							else if (speed == fastest_speed) fastest_count += 1;
						}

						// All pawns on the square are allies.
						// If this is failback for one of the pawns, make the others wait it.
						// Otherwise, choose randomly one of the fastest and don't make it wait the others.
						if ((square[0]->failback.x == x) && (square[0]->failback.y == y))
						{
							for(i = 1; (i < OVERLAP_LIMIT) && square[i]; ++i)
							{
								if (pawn_wait(occupied, square[i], step) < 0) return -1;
								pawn_detour(occupied, pawn, x, y);
							}
						}
						else
						{
							fastest_offset = (fastest_count > 1) ? (random() % fastest_count) : 0;
							for(i = 0; (i < OVERLAP_LIMIT) && square[i]; ++i)
							{
								if (square[i]->slot->unit->speed == fastest_speed)
								{
									if (fastest_offset) fastest_offset -= 1;
									else continue;
								}
								if (pawn_wait(occupied, square[i], step) < 0) return -1;
								pawn_detour(occupied, pawn, x, y);
							}
						}
						continue;

ready:
						// There are enemies on the square.
						for(i = 0; (i < OVERLAP_LIMIT) && square[i]; ++i)
						{
							if (pawn_stop(occupied, square[i], step) < 0) return -1;
							pawn_detour(occupied, pawn, x, y);
						}
					}
				}
			}
		} while (changed);

		// Update the failback of each pawn to the field at the top left side of the pawn.
		for(p = 0; p < pawns_count; ++p)
		{
			pawns[p].failback.x = pawns[p].step.x & ~0x1u;
			pawns[p].failback.y = pawns[p].step.y & ~0x1u;
		}
	}

	return 0;
}

void battlefield_movement_perform(struct battlefield battlefield[][BATTLEFIELD_HEIGHT], struct pawn *restrict pawns, size_t pawns_count)
{
	size_t p;
	struct pawn *pawn;

	struct queue_item *current, *next, *item;
	double distance;

	// Update occupied squares for each pawn.
	for(p = 0; p < pawns_count; ++p)
	{
		pawn = pawns + p;
		current = pawn_location(&pawn->moves, 1, &pawn->step);

		if (current)
		{
			// Remove finished moves.
			item = pawn->moves.first;
			while (item != current)
			{
				next = item->next;
				free(item);
				item = next;
			}
			pawn->moves.first = current;

			current->data.location = pawn->step;
			current->data.distance = 0;
			current->data.time = 0;

			while (next = current->next)
			{
				distance = battlefield_distance(current->data.location, next->data.location);
				next->data.distance = current->data.distance + distance;
				next->data.time = current->data.time + distance / pawn->slot->unit->speed;
				current = next;
			}
		}
		else
		{
			moves_free(pawn->moves.first->next);
			pawn->moves.first->next = 0;

			pawn->moves.first->data.location = pawn->step;
			pawn->moves.first->data.distance = 0;
			pawn->moves.first->data.time = 0;
		}
	}
}

void battlefield_clean_corpses(struct battle *battle)
{
	size_t p;
	struct pawn *pawn;
	struct slot *slot;
	for(p = 0; p < battle->pawns_count; ++p)
	{
		pawn = battle->pawns + p;
		slot = pawn->slot;

		if (!slot->count) continue;

		if (pawn->hurt >= (slot->count * slot->unit->health))
		{
			slot->count = 0;
			battle->field[pawn->moves.first->data.location.y][pawn->moves.first->data.location.x].pawn = 0;
		}
	}
}

/*int battlefield_reachable(const struct player *restrict players, struct pawn *battlefield[BATTLEFIELD_HEIGHT][BATTLEFIELD_WIDTH], const struct pawn *restrict pawn, unsigned char x, unsigned char y)
{
	const struct pawn *item;
	unsigned char alliance = players[pawn->slot->player].alliance;
	unsigned char speed = pawn->slot->unit->speed;

	// Determine pawn speed.
	item = battlefield[pawn->move.y[0]][pawn->move.x[0]];
	do
	{
		if (players[item->slot->player].alliance != alliance)
		{
			speed -= 1;
			break;
		}
	} while (item = item->_next);

	// Determine the euclidean distance between the two fields. Round the value to integer.
	int dx = x - pawn->move.x[0], dy = y - pawn->move.y[0];
	unsigned distance = round(sqrt(dx * dx + dy * dy));

	return (distance <= speed);
}*/

int battlefield_shootable(const struct pawn *restrict pawn, struct point target)
{
	// Only ranged units can shoot.
	if (!pawn->slot->unit->shoot) return 0;

	unsigned distance = round(battlefield_distance(pawn->moves.first->data.location, target));
	return (distance <= pawn->slot->unit->range);
}

int battlefield_init(const struct game *restrict game, struct battle *restrict battle, struct region *restrict region)
{
	struct slot **slots, *slot;
	struct pawn *pawns;
	size_t count;
	struct move move;

	size_t i;

	count = 0;
	for(slot = region->slots; slot; slot = slot->_next)
		count += 1;
	pawns = malloc(count * sizeof(*pawns));
	if (!pawns) return -1;

	// Sort the slots by speed descending.
	slots = malloc(count * sizeof(*slots));
	struct heap heap = {.data = slots, .count = count};
	if (!slots)
	{
		free(pawns);
		return -1;
	}
	i = 0;
	for(slot = region->slots; slot; slot = slot->_next) slots[i++] = slot;
	heapify(&heap);
	while (--i)
	{
		slot = heap.data[0];
		heap_pop(&heap);
		slots[i] = slot;
	}

	memset(battle->player_pawns, 0, sizeof(battle->player_pawns));

	// Initialize a pawn for each slot.
	for(i = 0; i < count; ++i)
	{
		pawns[i].slot = slots[i];
		pawns[i].hurt = 0;
		pawns[i].moves;

		move.location = POINT_NONE;
		move.distance = 0;
		move.time = 0;

		queue_init(&pawns[i].moves);
		queue_push(&pawns[i].moves, move);

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

int battle_end(struct battle *restrict battle, unsigned char host)
{
	return 0;
}
