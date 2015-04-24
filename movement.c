#include "types.h"
#include "map.h"
#include "pathfinding.h"
#include "battle.h"
#include "movement.h"

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

int movement_set(struct pawn *restrict pawn, struct point target, struct adjacency_list *restrict nodes)
{
	// TODO better handling of memory errors

	// Erase moves but remember their count so that we can restore them if necessary.
	size_t moves_count = pawn->moves_count;
	pawn->moves_count = 1;

	int error = path_find(pawn, target, nodes, 0, 0);
	if (error) pawn->moves_count = moves_count; // restore moves

	return error;

	/*else
	{
		if (pawn->moves[pawn->moves_count - 1].time <= 1.0)
			return 0; // Success if the pawn can reach its target in one round.
		else
		{
			double x, y;
			size_t index;

			// TODO this is buggy

			// Keep only the moves that will be done in one round.
			index = movement_location(pawn, 1.0, &x, &y);
			pawn->moves[index].location.x = x;
			pawn->moves[index].location.y = y;
			pawn->moves_count = index + 1;

			return ERROR_PROGRESS;
		}
	}*/
}

int movement_queue(struct pawn *restrict pawn, struct point target, struct adjacency_list *restrict nodes)
{
	// TODO better handling of memory errors

	int error = path_find(pawn, target, nodes, 0, 0);
	return error;
}

void movement_stay(struct pawn *restrict pawn)
{
	pawn->moves_count = 1;
}
