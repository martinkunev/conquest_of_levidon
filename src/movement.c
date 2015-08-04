#include <stdlib.h>
#include <string.h>

#include "errors.h"
#include "map.h"
#include "pathfinding.h"
#include "battle.h"
#include "movement.h"

#define MOVEMENT_STEPS (UNIT_SPEED_LIMIT * 2)

// Due to pawn dimensions, at most 4 pawns can try to move to a given square at the same time.
#define OVERLAP_LIMIT 4

/*
Each field in the battlefield is divided into four squares. Each pawn occupies four squares.
The movement is divided into MOVEMENT_STEPS steps (ensuring that each pawn moves at most one square per step).
For each pawn a failback field is chosen for each step. No two pawns can have the same failback field at a given step.
When two pawns try to occupy the same square at a given step, their movement is changed to avoid that.

When enemy pawns try to occupy the same square, they are redirected to their failback locations.
When allied pawns try to occupy the same squre, one of them stays where it is and the other are detoured to their failback locations to wait.
*/

// Returns the index of the first not yet reached move location or pawn->moves_count if there is no unreached location. Sets current location in real_x and real_y.
size_t movement_location(const struct pawn *restrict pawn, double time_now, double *restrict real_x, double *restrict real_y)
{
	double progress; // progress of the current move; 0 == start point; 1 == end point

	if (time_now < pawn->moves[0].time)
	{
		// The pawn has not started moving yet.
		*real_x = pawn->moves[0].location.x;
		*real_y = pawn->moves[0].location.y;
		return 0;
	}

	size_t i;
	for(i = 1; i < pawn->moves_count; ++i)
	{
		double time_start = pawn->moves[i - 1].time;
		double time_end = pawn->moves[i].time;
		if (time_now >= time_end) continue; // this move is already done

		progress = (time_now - time_start) / (time_end - time_start);
		*real_x = pawn->moves[i].location.x * progress + pawn->moves[i - 1].location.x * (1 - progress);
		*real_y = pawn->moves[i].location.y * progress + pawn->moves[i - 1].location.y * (1 - progress);

		return i;
	}

	// The pawn has reached its final location.
	*real_x = pawn->moves[pawn->moves_count - 1].location.x;
	*real_y = pawn->moves[pawn->moves_count - 1].location.y;
	return pawn->moves_count;
}

void pawn_place(struct pawn *restrict pawn, struct point location)
{
	pawn->moves[0].location = location;
	pawn->moves[0].time = 0.0;
	pawn->moves_count = 1;
}

void movement_stay(struct pawn *restrict pawn)
{
	pawn->moves_count = 1;
}

int movement_queue(struct pawn *restrict pawn, struct point target, struct adjacency_list *restrict nodes, const struct obstacles *restrict obstacles)
{
	return path_queue(pawn, target, nodes, obstacles);
}

int movement_follow(struct pawn *restrict pawn, const struct pawn *restrict target, struct adjacency_list *restrict graph, const struct obstacles *restrict obstacles)
{
	size_t i;

	int status;

	status = movement_queue(pawn, target->moves[0].location, graph, obstacles);
	if (status < 0) return status;

	for(i = 1; i < target->moves_count; ++i)
	{
		double time_start, time_end;
		double target_duration;

		target_duration = target->moves[i].time - target->moves[i - 1].time;
		time_start = pawn->moves[pawn->moves_count - 1].time;

		// TODO in the case of missing, move while possible?
		status = movement_queue(pawn, target->moves[i].location, graph, obstacles);
		if (status < 0) return status;

		time_end = pawn->moves[pawn->moves_count - 1].time;

		if (time_end - time_start < target_duration)
		{
			pawn->moves[pawn->moves_count - 1].time = time_start + target_duration;
			time_end = pawn->moves[pawn->moves_count - 1].time;
		}
	}

	return 0;
}

// Moves the pawn to the closest position for attacking the specified target.
// ERROR_MEMORY
// ERROR_MISSING - there is no attacking position available
int movement_attack(struct pawn *restrict pawn, struct point target, const struct battlefield field[BATTLEFIELD_HEIGHT][BATTLEFIELD_WIDTH], double reachable[BATTLEFIELD_HEIGHT][BATTLEFIELD_WIDTH], struct adjacency_list *restrict graph, const struct obstacles *restrict obstacles)
{
	if (!battlefield_neighbors(pawn->moves[pawn->moves_count - 1].location, target))
	{
		double move_distance = INFINITY;
		int move_x, move_y;

		int x = target.x, y = target.y;

		if ((x > 0) && !field[y][x - 1].pawn && (reachable[y][x - 1] < move_distance))
		{
			move_x = x - 1;
			move_y = y;
			move_distance = reachable[move_y][move_x];
		}
		if ((x < (BATTLEFIELD_WIDTH - 1)) && !field[y][x + 1].pawn && (reachable[y][x + 1] < move_distance))
		{
			move_x = x + 1;
			move_y = y;
			move_distance = reachable[move_y][move_x];
		}
		if ((y > 0) && !field[y - 1][x].pawn && (reachable[y - 1][x] < move_distance))
		{
			move_x = x;
			move_y = y - 1;
			move_distance = reachable[move_y][move_x];
		}
		if ((y < (BATTLEFIELD_HEIGHT - 1)) && !field[y + 1][x].pawn && (reachable[y + 1][x] < move_distance))
		{
			move_x = x;
			move_y = y + 1;
			move_distance = reachable[move_y][move_x];
		}

		if (move_distance < INFINITY)
			return movement_queue(pawn, (struct point){move_x, move_y}, graph, obstacles);
		else
			return ERROR_MISSING;
	}

	return 0;
}

// Plan the necessary movements for a pawn to attack its target.
int movement_attack_plan(struct pawn *restrict pawn, struct adjacency_list *restrict graph, const struct obstacles *restrict obstacles)
{
	int status;

	if (pawn->action != PAWN_FIGHT) return 0;

	pawn->moves_manual = pawn->moves_count;

	// Nothing to do if the target pawn is immobile.
	if (pawn->target.pawn->moves_count == 1) return 0;

	status = movement_follow(pawn, pawn->target.pawn, graph, obstacles);
	switch (status)
	{
	case ERROR_MISSING: // target pawn is not reachable
		// TODO move until possible (the field before the wall)
		movement_stay(pawn);
		pawn->action = 0;
		return 0;

	default:
		return status;
	}

	return 0;
}

static struct point location_field(const struct point location)
{
	struct point square;
	square.x = location.x / 2;
	square.y = location.y / 2;
	return square;
}

// Finds the top-left square that the pawn occupies at moment time_now.
static size_t pawn_position(const struct pawn *restrict pawn, double time_now, struct point *restrict location)
{
	double real_x, real_y;
	size_t index = movement_location(pawn, time_now, &real_x, &real_y);

	location->x = real_x * 2 + 0.5;
	location->y = real_y * 2 + 0.5;
	return index;
}

// Places pawn on a square.
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

// Detours a pawn to its failback. Updates occupied squares information.
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

static int pawn_wait(struct pawn *occupied[BATTLEFIELD_HEIGHT * 2][BATTLEFIELD_WIDTH * 2][OVERLAP_LIMIT], struct pawn *pawn, unsigned step_stop, unsigned step_continue)
{
	// Make the pawn detour to its failback at step_stop the previous step and continue moving toward its next location at step_continue.

	double time_detour = (double)step_stop / MOVEMENT_STEPS;

	if (time_detour > pawn->moves[0].time)
	{
		// The pawn has started moving at the time of the detour.

		struct point location;
		double distance;
		size_t index = pawn_position(pawn, time_detour, &location);
		if (index == pawn->moves_count) return 0; // the pawn is immobile

		// Make the pawn wait at its present location.
		memmove(pawn->moves + index + 1, pawn->moves + index, (pawn->moves_count - index) * sizeof(*pawn->moves));
		pawn->moves_count += 1;
		pawn->moves[index].location = location_field(location);
		distance = battlefield_distance(pawn->moves[index - 1].location, pawn->moves[index].location);
		pawn->moves[index].time = pawn->moves[index - 1].time + distance / pawn->troop->unit->speed;

		index += 1;

		// Make the failback location the pawn's next location.
		if (index < pawn->moves_count)
			memmove(pawn->moves + index + 1, pawn->moves + index, (pawn->moves_count - index) * sizeof(*pawn->moves));
		pawn->moves_count += 1;
		pawn->moves[index].location = location_field(pawn->failback);
		pawn->moves[index].time = (double)step_continue / MOVEMENT_STEPS;

		// Update time for the following movements.
		for(index += 1; index < pawn->moves_count; ++index)
		{
			distance = battlefield_distance(pawn->moves[index - 1].location, pawn->moves[index].location);
			pawn->moves[index].time = pawn->moves[index - 1].time + distance / pawn->troop->unit->speed;
		}
	}
	else
	{
		// The pawn is at its failback location at the time of the detour.
		double wait = ((double)step_continue / MOVEMENT_STEPS) - pawn->moves[0].time;
		size_t i;
		for(i = 0; i < pawn->moves_count; ++i)
			pawn->moves[i].time += wait;
	}

	return 0;
}

static int pawn_stop(struct pawn *occupied[BATTLEFIELD_HEIGHT * 2][BATTLEFIELD_WIDTH * 2][OVERLAP_LIMIT], struct pawn *pawn, unsigned step)
{
	// Collisions can only occur after the start of the movement.
	// assert(step);

	// Make the pawn detour to its failback at the previous step.

	double time_detour = ((double)step - 1) / MOVEMENT_STEPS;

	if (time_detour > pawn->moves[0].time)
	{
		// The pawn has started moving at the time of the detour.

		struct point location;
		size_t index = pawn_position(pawn, time_detour, &location);
		if (index == pawn->moves_count) return 0; // the pawn is immobile

		pawn->moves[index].location = location_field(location);
		pawn->moves[index].time = time_detour;

		if (!point_eq(location, pawn->failback))
		{
			index += 1; // TODO range check?

			// Make the failback location the pawn's next location.
			pawn->moves[index].location = location_field(pawn->failback);
			pawn->moves[index].time = (double)step / MOVEMENT_STEPS;
		}

		pawn->moves_count = index + 1;
	}
	else
	{
		// The pawn is at its failback location at the time of the detour.
		// Cancel all the moves.
		movement_stay(pawn);
	}

	return 0;
}

// Returns the step at which the pawn will first change its position or -1 if it will not move until the end of the round.
static int pawn_move_step(struct pawn *pawns[OVERLAP_LIMIT], size_t pawn_index, unsigned step)
{
	size_t i;
	unsigned step_next;
	for(step_next = step + 1; step_next <= MOVEMENT_STEPS; ++step_next)
	{
		struct point next;
		double time_next = (double)step_next / MOVEMENT_STEPS;
		pawn_position(pawns[pawn_index], time_next, &next);

		if (!point_eq(next, pawns[pawn_index]->step))
		{
			for(i = 0; (i < OVERLAP_LIMIT) && pawns[i]; ++i)
			{
				if (i == pawn_index) continue;

				// Check if the pawn will collide when it moves.
				struct point wait = pawns[i]->failback;
				if ((abs((int)next.x - (int)wait.x) < 2) && (abs((int)next.y - (int)wait.y) < 2))
					return -1;
			}

			return step_next;
		}
	}
	return -1;
}

static void collision_resolve(const struct player *restrict players, struct pawn *occupied[BATTLEFIELD_HEIGHT * 2][BATTLEFIELD_WIDTH * 2][OVERLAP_LIMIT], size_t x, size_t y, unsigned step)
{
	size_t i;

	struct pawn *pawns[] = {occupied[y][x][0], occupied[y][x][1], occupied[y][x][2], occupied[y][x][3]}; // TODO this hardcodes OVERLAP_LIMIT == 4

	unsigned char alliance = players[pawns[0]->troop->owner].alliance;
	for(i = 1; (i < OVERLAP_LIMIT) && pawns[i]; ++i)
		if (players[pawns[i]->troop->owner].alliance != alliance)
		{
			// There are enemies on the square.
			for(i = 0; (i < OVERLAP_LIMIT) && pawns[i]; ++i)
			{
				if (pawn_stop(occupied, pawns[i], step) < 0) return; // TODO memory error?
				pawn_detour(occupied, pawns[i]);
			}
			return;
		}

	// All pawns on the square are allies.
	// Choose one of the pawns to continue its movement.
	// If a pawn is at its failback position, the others have to wait for it.
	// Otherwise find a pawn that can move while the others wait for it.

	size_t keep_index;
	int step_next;

	if (point_eq(pawns[0]->step, pawns[0]->failback))
	{
		keep_index = 0;
		step_next = pawn_move_step(pawns, keep_index, step);
		// assert(step_next);
		if (step_next > 0) goto wait;
	}
	else for(keep_index = 0; (keep_index < OVERLAP_LIMIT) && pawns[keep_index]; ++keep_index)
	{
		step_next = pawn_move_step(pawns, keep_index, step);
		// assert(step_next);
		if (step_next > 0) goto wait;
	}

	// No pawn can continue moving. Make all pawns wait the end of the round.
	for(i = 0; (i < OVERLAP_LIMIT) && pawns[i]; ++i)
	{
		if (pawn_wait(occupied, pawns[i], step - 1, MOVEMENT_STEPS) < 0) return; // TODO memory error?
		pawn_detour(occupied, pawns[i]);
	}

	return;

wait:
	// Keep the movement plan of keep_index.
	// Make all the other pawns wait for it at their failback locations.
	for(i = 0; (i < OVERLAP_LIMIT) && pawns[i]; ++i)
	{
		if (i == keep_index) continue;

		if (pawn_wait(occupied, pawns[i], step - 1, step_next - 1) < 0) return; // TODO memory error?
		pawn_detour(occupied, pawns[i]);
	}
}

// pawns must be sorted by speed descending
void battlefield_movement_plan(const struct player *restrict players, size_t players_count, struct battlefield battlefield[][BATTLEFIELD_HEIGHT], struct pawn *restrict pawns, size_t pawns_count)
{
	unsigned step;
	double now;

	size_t p;
	struct pawn *pawn;

	// Store a list of the pawns occupying a given square.
	// Pawn at its failback location is placed first in the list.
	struct pawn *occupied[BATTLEFIELD_HEIGHT * 2][BATTLEFIELD_WIDTH * 2][OVERLAP_LIMIT];

	size_t x, y;
	size_t i;

	int failback;

	// Set failback and movement time for each pawn.
	for(p = 0; p < pawns_count; ++p)
	{
		if (!pawns[p].count) continue;

		pawns[p].failback.x = pawns[p].moves[0].location.x * 2;
		pawns[p].failback.y = pawns[p].moves[0].location.y * 2;

		pawns[p].moves[0].time = 0;
		for(i = 1; i < pawns[p].moves_count; ++i)
		{
			double distance = battlefield_distance(pawns[p].moves[i - 1].location, pawns[p].moves[i].location);
			pawns[p].moves[i].time = pawns[p].moves[i - 1].time + distance / pawns[p].troop->unit->speed;
		}
	}

	for(step = 1; step <= MOVEMENT_STEPS; ++step)
	{
		now = (double)step / MOVEMENT_STEPS;

		// Update occupied squares for each pawn.
		memset(occupied, 0, sizeof(occupied));
		for(p = 0; p < pawns_count; ++p)
		{
			pawn = pawns + p;
			if (!pawn->count) continue;
			pawn_position(pawn, now, &pawn->step);

			failback = point_eq(pawn->step, pawn->failback);
			square_occupy(occupied[pawn->step.y][pawn->step.x], pawn, failback);
			square_occupy(occupied[pawn->step.y][pawn->step.x + 1], pawn, failback);
			square_occupy(occupied[pawn->step.y + 1][pawn->step.x], pawn, failback);
			square_occupy(occupied[pawn->step.y + 1][pawn->step.x + 1], pawn, failback);
		}

		// Detect and resolve pawn collisions.
		// If the overlapping pawns are enemies, make them stay and fight.
		// If the overlapping pawns are allies, make the slower pawns wait the faster pass.
		// Repeat as long as pawn movement changed during the last pass.
		// Each repetition causes at least one pawn to move to its failback. This ensures the algorithm will finish.
retry:
		for(y = 0; y < BATTLEFIELD_HEIGHT * 2; ++y)
		{
			for(x = 0; x < BATTLEFIELD_WIDTH * 2; ++x)
			{
				if (occupied[y][x][1]) // movement overlap
				{
					collision_resolve(players, occupied, x, y, step);
					goto retry;
				}
			}
		}

		// Update the failback of each pawn to the field at the top left side of the pawn.
		for(p = 0; p < pawns_count; ++p)
		{
			if (!pawns[p].count) continue;

			pawns[p].failback.x = pawns[p].step.x & ~0x1u;
			pawns[p].failback.y = pawns[p].step.y & ~0x1u;
		}
	}

	// Make sure the position of the pawn after the round is stored as a move entry.
	for(p = 0; p < pawns_count; ++p)
	{
		pawn = pawns + p;
		if (!pawn->count) continue;

		size_t index = pawn_position(pawn, 1.0, &pawn->step);

		if (index < pawn->moves_count)
		{
			memmove(pawn->moves + index + 1, pawn->moves + index, (pawn->moves_count - index) * sizeof(*pawn->moves));
			pawn->moves_count += 1;

			pawn->moves[index].location = location_field(pawn->step);
			pawn->moves[index].time = 1.0;
		}
	}
}

// Update pawn position, action and movement.
int battlefield_movement_perform(struct battle *restrict battle, struct pawn *restrict pawn, struct adjacency_list *restrict graph, const struct obstacles *restrict obstacles)
{
	size_t i;

	size_t index = pawn_position(pawn, 1.0, &pawn->step);

	// assert(index);
	index -= 1;

	// Change pawn position on the battlefield.
	struct point location_old = pawn->moves[0].location;
	struct point location_new = location_field(pawn->step);
	if (battle->field[location_old.y][location_old.x].pawn == pawn)
		battle->field[location_old.y][location_old.x].pawn = 0;
	battle->field[location_new.y][location_new.x].pawn = pawn;

	if (index < pawn->moves_count)
	{
		// Remove finished moves.
		if (index)
		{
			pawn->moves_count -= index;
			pawn->moves_manual -= index;

			memmove(pawn->moves, pawn->moves + index, pawn->moves_count * sizeof(*pawn->moves));
		}
		pawn->moves[0].location = location_new;
		pawn->moves[0].time = 0.0;
		for(i = 1; i < pawn->moves_count; ++i)
		{
			double distance = battlefield_distance(pawn->moves[i - 1].location, pawn->moves[i].location);
			pawn->moves[i].time = pawn->moves[i - 1].time + distance / pawn->troop->unit->speed;
		}

		// If the pawn follows another pawn, update its movement to correspond to the current battlefield situation.
		if (pawn->action == PAWN_FIGHT)
		{
			int status;

			// Delete automatically generated moves.
			if (pawn->moves_manual < pawn->moves_count)
				pawn->moves_count = pawn->moves_manual;

			status = movement_follow(pawn, pawn->target.pawn, graph, obstacles);
			if (status == ERROR_MISSING) 
			{
				movement_stay(pawn);
				pawn->action = 0;
			}
			else return status;
		}
	}
	else
	{
		movement_stay(pawn);
		pawn->moves[0].location = location_new;
		pawn->moves[0].time = 0.0;

		// TODO remove fight target if it is unreachable
	}

	return 0;
}
