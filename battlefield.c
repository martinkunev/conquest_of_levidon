#include <stdlib.h>
#include <string.h>

#include "types.h"
#include "json.h"
#include "map.h"
#include "battlefield.h"
#include "pathfinding.h"

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

static struct point location_field(const struct point location)
{
	struct point square;
	square.x = location.x / 2;
	square.y = location.y / 2;
	return square;
}

// Returns the first movement that is not finished or 0 if there is no such move. Sets current position in location.
static struct queue_item *pawn_location(const struct queue *restrict moves, double time_now, struct point *restrict location)
{
	struct queue_item *item;

	double time_start, time_end;
	double progress; // progress of the current move; 0 == start point; 1 == end point

	double real_x, real_y;

	for(item = moves->first; item->next; item = item->next)
	{
		time_start = item->data.time;
		if (time_now < time_start) // this move has not started yet
		{
			*location = moves->first->data.location;
			return item;
		}
		time_end = item->next->data.time;
		if (time_now >= time_end) continue; // this move is already done

		progress = (time_now - time_start) / (time_end - time_start);

		real_x = item->next->data.location.x * progress + item->data.location.x * (1 - progress);
		real_y = item->next->data.location.y * progress + item->data.location.y * (1 - progress);

		location->x = real_x * 2;
		location->y = real_y * 2;
		return item;
	}

	location->x = moves->last->data.location.x * 2;
	location->y = moves->last->data.location.y * 2;
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

// Removes pawn from a square (if the pawn occupies it).
static inline void square_clear(struct pawn **occupied, const struct pawn *restrict pawn)
{
	size_t i, last = OVERLAP_LIMIT - 1;
	while (!occupied[last]) last -= 1;
	for(i = last; occupied[i] != pawn; --i)
		if (!i)
			break;
	if (i != last) occupied[i] = occupied[last];
	occupied[last] = 0;
}

static void pawn_detour(struct pawn *occupied[BATTLEFIELD_HEIGHT * 2][BATTLEFIELD_WIDTH * 2][OVERLAP_LIMIT], struct pawn *pawn)
{
	unsigned x, y;

	x = pawn->step.x;
	y = pawn->step.y;
	square_clear(occupied[y][x], pawn);
	square_clear(occupied[y][x + 1], pawn);
	square_clear(occupied[y + 1][x], pawn);
	square_clear(occupied[y + 1][x + 1], pawn);

	pawn->step = pawn->failback;

	x = pawn->step.x;
	y = pawn->step.y;
	square_occupy(occupied[y][x], pawn, 1);
	square_occupy(occupied[y][x + 1], pawn, 1);
	square_occupy(occupied[y + 1][x], pawn, 1);
	square_occupy(occupied[y + 1][x + 1], pawn, 1);
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

static int pawn_wait(struct pawn *occupied[BATTLEFIELD_HEIGHT * 2][BATTLEFIELD_WIDTH * 2][OVERLAP_LIMIT], struct pawn *pawn, unsigned step)
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

		next->data.location = location_field(location);
		next->data.distance = current->data.distance + battlefield_distance(current->data.location, next->data.location);
		next->data.time = time_detour;

		if (!point_eq(location, pawn->failback))
		{
			current = next;
			if (queue_insert(&pawn->moves, current, move) < 0) return -1;
			next = current->next;

			// Make the failback location the pawn's next location.
			next->data.location = location_field(pawn->failback);
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

static int pawn_stop(struct pawn *occupied[BATTLEFIELD_HEIGHT * 2][BATTLEFIELD_WIDTH * 2][OVERLAP_LIMIT], struct pawn *pawn, unsigned step)
{
	// Collisions can only occur after the start of the movement.
	// assert(step);

	// Make the pawn detour to its failback at the previous step.

	double time_detour = ((double)step - 1) / MOVEMENT_STEPS;

	if (time_detour > pawn->moves.first->data.time)
	{
		// The pawn has started moving at the time of the detour.

		struct queue_item *current, *next;

		struct point location;
		current = pawn_location(&pawn->moves, time_detour, &location);

		if (!current) return 0; // TODO is this right?

		// assert(current);
		// assert(current->next);

		next = current->next;

		next->data.location = location_field(location);
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
			next->data.location = location_field(pawn->failback);
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

// pawns must be sorted by speed descending
int battlefield_movement_plan(const struct player *restrict players, size_t players_count, struct battlefield battlefield[][BATTLEFIELD_HEIGHT], struct pawn *restrict pawns, size_t pawns_count)
{
	unsigned step;
	double now;

	size_t p;
	struct pawn *pawn;

	// Store a list of the pawns occupying a given square.
	// Pawn at its failback location is placed first in the list.
	struct pawn *occupied[BATTLEFIELD_HEIGHT * 2][BATTLEFIELD_WIDTH * 2][OVERLAP_LIMIT];
	struct pawn **square;

	size_t x, y;
	size_t i;

	unsigned char alliance;
	unsigned char speed;
	unsigned char fastest_speed, fastest_count, fastest_offset;

	int failback;
	int changed;

	for(p = 0; p < pawns_count; ++p)
	{
		pawns[p].failback.x = pawns[p].moves.first->data.location.x * 2;
		pawns[p].failback.y = pawns[p].moves.first->data.location.y * 2;

		struct queue_item *item;
		pawns[p].moves.first->data.time = 0;
		for(item = pawns[p].moves.first; item->next; item = item->next)
			item->next->data.time = item->next->data.distance / pawns[p].slot->unit->speed;
	}

	for(step = 0; step < MOVEMENT_STEPS; ++step)
	{
		now = (double)step / MOVEMENT_STEPS;

		// Update occupied squares for each pawn.
		memset(occupied, 0, sizeof(occupied));
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

			for(y = 0; y < BATTLEFIELD_HEIGHT * 2; ++y)
			{
				for(x = 0; x < BATTLEFIELD_WIDTH * 2; ++x)
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
							if (players[square[i]->slot->player].alliance != alliance) goto enemy;

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
								pawn_detour(occupied, pawn);
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
								pawn_detour(occupied, pawn);
							}
						}
						continue;

enemy:
						// There are enemies on the square.
						{
							struct pawn *pawns[] = {square[0], square[1], square[2], square[3]}; // TODO this hardcodes OVERLAP_LIMIT == 4
							for(i = 0; (i < OVERLAP_LIMIT) && pawns[i]; ++i)
							{
								if (pawn_stop(occupied, pawns[i], step) < 0) return -1;
								pawn_detour(occupied, pawns[i]);
							}
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

	struct queue_item *move_next, *next, *item;
	double distance;

	// Update occupied squares for each pawn.
	for(p = 0; p < pawns_count; ++p)
	{
		pawn = pawns + p;
		move_next = pawn_location(&pawn->moves, 1, &pawn->step);

		// Change pawn position on the battlefield.
		struct point location_old = pawn->moves.first->data.location;
		struct point location_new = location_field(pawn->step);
		if (battlefield[location_old.y][location_old.x].pawn == pawn)
			battlefield[location_old.y][location_old.x].pawn = 0;
		battlefield[location_new.y][location_new.x].pawn = pawn;

		if (move_next)
		{
			// Remove finished moves.
			item = pawn->moves.first;
			while (item != move_next)
			{
				next = item->next;
				free(item);
				item = next;
			}
			pawn->moves.first = move_next;

			move_next->data.location = location_new;
			move_next->data.distance = 0;
			move_next->data.time = 0;

			while (next = move_next->next)
			{
				distance = battlefield_distance(move_next->data.location, next->data.location);
				next->data.distance = move_next->data.distance + distance;
				next->data.time = move_next->data.time + distance / pawn->slot->unit->speed;
				move_next = next;
			}
		}
		else
		{
			pawn_stay(pawn);

			pawn->moves.first->data.location = location_new;
			pawn->moves.first->data.distance = 0;
			pawn->moves.first->data.time = 0;
		}
	}
}

static void pawn_deal(struct pawn *pawn, unsigned damage)
{
	if (!pawn) return;
	pawn->hurt += damage;
}

void battlefield_fight(const struct game *restrict game, struct battle *restrict battle)
{
	size_t i, j;
	for(i = 0; i < battle->pawns_count; ++i)
	{
		struct pawn *fighter = battle->pawns + i;
		unsigned char fighter_alliance = game->players[fighter->slot->player].alliance;

		int x = fighter->moves.first->data.location.x;
		int y = fighter->moves.first->data.location.y;
		unsigned damage_total = fighter->slot->unit->damage * fighter->slot->count;
		unsigned damage;

		struct pawn *victims[4], *victim;
		unsigned enemies_count = 0;

		// Look for pawns to fight at the neighboring fields.
		enemies_count = 0;
		if ((x > 0) && (victim = battle->field[y][x - 1].pawn) && (game->players[victim->slot->player].alliance != fighter_alliance))
			victims[enemies_count++] = victim;
		if ((x < (BATTLEFIELD_WIDTH - 1)) && (victim = battle->field[y][x + 1].pawn) && (game->players[victim->slot->player].alliance != fighter_alliance))
			victims[enemies_count++] = victim;
		if ((y > 0) && (victim = battle->field[y - 1][x].pawn) && (game->players[victim->slot->player].alliance != fighter_alliance))
			victims[enemies_count++] = victim;
		if ((y < (BATTLEFIELD_HEIGHT - 1)) && (victim = battle->field[y + 1][x].pawn) && (game->players[victim->slot->player].alliance != fighter_alliance))
			victims[enemies_count++] = victim;
		if (!enemies_count) continue; // nothing to fight

		for(j = 0; j < enemies_count; ++j)
		{
			damage = (unsigned)((double)damage_total / enemies_count + 0.5);
			pawn_deal(victims[j], damage);
		}
	}
}

void battlefield_shoot(struct battle *battle)
{
	const double targets[3][2] = {{1, 0.5}, {0, 0.078125}, {0, 0.046875}}; // 1/2, 5/64, 3/64

	size_t i;
	for(i = 0; i < battle->pawns_count; ++i)
	{
		struct pawn *shooter = battle->pawns + i;

		if (!shooter->slot->count) continue;
		if (point_eq(shooter->shoot, POINT_NONE)) continue;

		int x = shooter->shoot.x;
		int y = shooter->shoot.y;
		unsigned damage_total = shooter->slot->unit->shoot * shooter->slot->count;
		unsigned damage;

		unsigned target_index;
		double distance, miss, on_target;

		distance = battlefield_distance(shooter->moves.first->data.location, shooter->shoot);
		miss = distance / shooter->slot->unit->range;

		// Shooters have some chance to hit a field adjacent to the target, depending on the distance.
		// Damage is dealt to the target field and to its neighbors.

		target_index = 0;
		on_target = targets[target_index][0] * (1 - miss) + targets[target_index][1] * miss;
		pawn_deal(battle->field[y][x].pawn, (unsigned)(damage * on_target + 0.5));

		target_index = 1;
		on_target = targets[target_index][0] * (1 - miss) + targets[target_index][1] * miss;
		damage = (unsigned)(damage_total * on_target + 0.5);
		if (x > 0) pawn_deal(battle->field[y][x - 1].pawn, damage);
		if (x < (BATTLEFIELD_WIDTH - 1)) pawn_deal(battle->field[y][x + 1].pawn, damage);
		if (y > 0) pawn_deal(battle->field[y - 1][x].pawn, damage);
		if (y < (BATTLEFIELD_HEIGHT - 1)) pawn_deal(battle->field[y + 1][x].pawn, damage);

		target_index = 2;
		on_target = targets[target_index][0] * (1 - miss) + targets[target_index][1] * miss;
		damage = (unsigned)(damage_total * on_target + 0.5);
		if (x > 0)
		{
			if (y > 0) pawn_deal(battle->field[y - 1][x - 1].pawn, damage);
			if (y < (BATTLEFIELD_HEIGHT - 1)) pawn_deal(battle->field[y + 1][x - 1].pawn, damage);
		}
		if (x < (BATTLEFIELD_WIDTH - 1))
		{
			if (y > 0) pawn_deal(battle->field[y - 1][x + 1].pawn, damage);
			if (y < (BATTLEFIELD_HEIGHT - 1)) pawn_deal(battle->field[y + 1][x + 1].pawn, damage);
		}

		// TODO ?deal more damage to moving pawns
	}
}

// Determine how many units to kill.
static unsigned pawn_victims(unsigned min, unsigned max)
{
	// The possible outcomes are all the integers in [min, max].
	if (max == min) return min;
	unsigned outcomes = (max - min + 1);

	// TODO ?use a better algorithm here
	// For the outcomes min and max there is 1 chance value (least probable).
	// Outcomes closer to the middle of the interval are more probable than the ones farther from it.
	// When going from the end of the interval to the middle, the number of chance values increases by 2 with every integer.
	// Example: for the interval [2, 6], the chance values are: 1, 3, 5, 3, 1

	unsigned chances, chance;
	chances = 2 * (outcomes / 2) * (outcomes / 2);
	if (outcomes % 2) chances += outcomes;
	chance = random() % chances;

	// Find the outcome corresponding to the chosen chance value.
	size_t distance = 0;
	if (chance < chances / 2)
	{
		// left half of the interval [min, max]
		while ((distance + 1) * (distance + 1) <= chance) distance += 1;
		return min + distance;
	}
	else
	{
		// right half of the interval [min, max]
		while ((distance + 1) * (distance + 1) <= (chances - chance)) distance += 1;
		return max - distance;
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

		if ((slot->count * slot->unit->health) <= pawn->hurt)
		{
			// All units in this pawn are killed.
			slot->count = 0;
			battle->field[pawn->moves.first->data.location.y][pawn->moves.first->data.location.x].pawn = 0;
		}
		else
		{
			// Find the minimum and maximum of units that can be killed.
			unsigned max = pawn->hurt / slot->unit->health;
			unsigned min;
			if ((slot->unit->health - 1) * slot->count >= pawn->hurt) min = 0;
			else min = pawn->hurt % slot->count;

			unsigned victims = pawn_victims(min, max);
			slot->count -= victims;
			pawn->hurt -= victims * slot->unit->health;
		}
	}
}

void pawn_stay(struct pawn *restrict pawn)
{
	moves_free(pawn->moves.first->next);
	pawn->moves.first->next = 0;
	pawn->moves.last = pawn->moves.first;
	pawn->moves.length = 1;
}

int battlefield_reachable(struct battlefield battlefield[][BATTLEFIELD_WIDTH], struct pawn *restrict pawn, struct point target)
{
	// TODO better handling of memory errors

	// TODO it's not necessary to do this every time
	struct vector_adjacency nodes = {0};
	if (visibility_graph_build(0, 0, &nodes)) abort();

	pawn_stay(pawn);

	if (path_find(&pawn->moves, target, &nodes, 0, 0)) return 0;

	visibility_graph_free(&nodes);

	if (pawn->moves.last->data.distance > pawn->slot->unit->speed)
	{
		// not reachable in one round
		pawn_stay(pawn);
		return 0;
	}

	return 1;
}

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
	for(slot = region->slots; slot; slot = slot->_next)
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

		queue_init(&pawns[i].moves);

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

	// TODO remove this
	// Put the pawns at their initial positions.
	for(i = 0; i < count; ++i)
	{
		struct move move;

		// TODO this will break if more than 4 slots come from a given region
		struct point positions_defend[4] = {
			{11, 11}, {12, 11}, {11, 12}, {12, 12}
		};
		struct point positions_attack[NEIGHBORS_LIMIT][4] = {
			{{23, 11}, {23, 12}, {23, 10}, {23, 13}},
			{{23, 0}, {22, 0}, {23, 1}, {22, 1}},
			{{11, 0}, {12, 0}, {10, 0}, {13, 0}},
			{{0, 0}, {1, 0}, {0, 1}, {1, 1}},
			{{0, 11}, {0, 12}, {0, 10}, {0, 13}},
			{{0, 23}, {0, 22}, {1, 23}, {1, 22}},
			{{11, 23}, {12, 23}, {10, 23}, {13, 23}},
			{{23, 23}, {22, 23}, {23, 22}, {22, 22}},
		};
		size_t progress_defend = 0;
		size_t progress_attack[NEIGHBORS_LIMIT] = {0};

		if (slots[i]->location == region) move.location = positions_defend[progress_defend++];
		else
		{
			size_t j;
			for(j = 0; j < NEIGHBORS_LIMIT; ++j)
				if (slots[i]->location == region->neighbors[j])
				{
					move.location = positions_attack[j][progress_attack[j]++];
					break;
				}
		}
		move.distance = 0;
		move.time = 0;
		queue_push(&pawns[i].moves, move);

		battle->field[move.location.y][move.location.x].pawn = pawns + i;
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
