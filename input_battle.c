#include <stdlib.h>
#include <string.h>
#include <sys/time.h>

#include <xcb/xcb.h>

#include "types.h"
#include "json.h"
#include "map.h"
#include "pathfinding.h"
#include "battlefield.h"
#include "input.h"
#include "input_battle.h"
#include "interface.h"

#define ANIMATION_DURATION 4.0

extern const struct battle *battle;
extern struct battlefield (*battlefield)[BATTLEFIELD_WIDTH];

// Sets move destination of a pawn. Returns -1 if the current player is not allowed to move the pawn at the destination.
static void pawn_move(struct pawn *restrict pawn, unsigned x, unsigned y)
{
	struct point target = {x, y};

	// TODO support fast move

	if (!battlefield_reachable(pawn, target, state.nodes)) return;

	// TODO set pawn->fight

	// Reset shoot commands.
	pawn->shoot = POINT_NONE;
}

// Sets shoot target of a pawn. Returns -1 if the current player is not allowed to shoot at the target with this pawn.
static void pawn_shoot(struct pawn *restrict pawn, unsigned x, unsigned y)
{
	struct point target = {x, y};

	if (!battlefield_shootable(pawn, target)) return;

	pawn->shoot = target;

	// Reset move commands.
	pawn_stay(pawn);
}

static int input_round(int code, unsigned x, unsigned y, uint16_t modifiers, const struct game *restrict game)
{
	return ((code == 'n') ? INPUT_DONE : 0);
}

static int input_none(int code, unsigned x, unsigned y, uint16_t modifiers, const struct game *restrict game)
{
	return 0;
}

static int input_field(int code, unsigned x, unsigned y, uint16_t modifiers, const struct game *restrict game)
{
	if (code == EVENT_MOTION) return INPUT_NOTME;
	if (code >= 0) return INPUT_NOTME;

	x /= FIELD_SIZE;
	y /= FIELD_SIZE;

	// if (modifiers & XCB_MOD_MASK_SHIFT) ; // TODO fast move

	if (code == -1)
	{
		// Set current field.
		state.x = x;
		state.y = y;
		state.selected.pawn = battlefield[y][x].pawn;
	}
	else if (code == -3)
	{
		struct pawn *pawn = state.selected.pawn;
		if (!pawn) return 0;
		if (pawn->slot->player != state.player) return 0;

		struct point target = {x, y};
		if (point_eq(target, pawn->moves[0].location))
		{
			pawn_stay(pawn);
			pawn->shoot = POINT_NONE;
			return 0;
		}

		// shoot if CONTROL is pressed; move otherwise
		if (modifiers & XCB_MOD_MASK_CONTROL) pawn_shoot(pawn, x, y);
		else pawn_move(pawn, x, y);
	}

	return 0;
}

static int input_place(int code, unsigned x, unsigned y, uint16_t modifiers, const struct game *restrict game)
{
	if (code == EVENT_MOTION) return INPUT_NOTME;
	if (code >= 0) return INPUT_NOTME;

	x /= FIELD_SIZE;
	y /= FIELD_SIZE;

	if (code == -3)
	{
		struct pawn *pawn = state.selected.pawn;
		if (!pawn) return 0;
		if (battlefield[y][x].pawn) return 0;

		const struct point *positions = formation_positions(pawn->slot, game->regions + state.region);
		struct point location = {x, y};

		size_t i;
		for(i = 0; i < PAWNS_LIMIT; ++i)
			if (point_eq(location, positions[i]))
			{
				pawn->moves[0].location = location;
				// TODO pawn->moves[0].time = 0.0;
				battlefield[y][x].pawn = pawn;
				state.selected.pawn = 0;

				// Finish formation input if all pawns are placed.
				for(i = 0; i < battle->player_pawns[state.player].length; ++i)
				{
					struct pawn *pawn = battle->player_pawns[state.player].data[i];
					if (point_eq(pawn->moves[0].location, POINT_NONE))
						return 0;
				}
				return INPUT_DONE;
			}
	}

	return 0;
}

static int input_pawn(int code, unsigned x, unsigned y, uint16_t modifiers, const struct game *restrict game)
{
	if (code == EVENT_MOTION) return INPUT_NOTME;
	if (code >= 0) return INPUT_NOTME;

	if (code == -1)
	{
		if ((x % (FIELD_SIZE + 1)) >= FIELD_SIZE) goto reset;
		if ((y % (FIELD_SIZE + MARGIN)) >= FIELD_SIZE) goto reset;

		// Select the clicked pawn.
		unsigned column = x / (FIELD_SIZE + 1);
		unsigned line = y / (FIELD_SIZE + MARGIN);
		size_t i;
		for(i = 0; i < battle->player_pawns[state.player].length; ++i)
		{
			struct pawn *pawn = battle->player_pawns[state.player].data[i];
			if (line)
			{
				if (pawn->slot->location != game->regions[state.region].neighbors[line - 1]) continue;
			}
			else
			{
				if (pawn->slot->location != game->regions + state.region) continue;
			}

			if (column) column -= 1;
			else
			{
				state.selected.pawn = pawn;
				return 0;
			}
		}
	}

reset:
	// Make sure no pawn is selected.
	state.selected.pawn = 0;
	return 0;
}

int input_battle(const struct game *restrict game, const struct battle *restrict battle, unsigned char player)
{
	if_set(battle->field, battle);

	struct area areas[] = {
		{
			.left = 0,
			.right = SCREEN_WIDTH - 1,
			.top = 0,
			.bottom = SCREEN_HEIGHT - 1,
			.callback = input_round
		},
		{
			.left = 0,
			.right = BATTLEFIELD_WIDTH * FIELD_SIZE - 1,
			.top = 0,
			.bottom = BATTLEFIELD_HEIGHT * FIELD_SIZE - 1,
			.callback = input_field
		},
	};

	state.player = player;

	// Set current field to a field outside of the board.
	state.x = BATTLEFIELD_WIDTH;
	state.y = BATTLEFIELD_HEIGHT;

	state.selected.pawn = 0;

	state.nodes = visibility_graph_build(0, 0);
	if (!state.nodes) abort();
	int status = input_local(if_battle, areas, sizeof(areas) / sizeof(*areas), game);
	visibility_graph_free(state.nodes);

	return status;
}

int input_formation_basic(const struct game *restrict game, const struct region *restrict region, const struct battle *restrict battle, unsigned char player)
{
	unsigned offset_defend = 0, offset_attack[NEIGHBORS_LIMIT] = {0};

	unsigned column;

	size_t i, j;
	for(i = 0; i < battle->player_pawns[player].length; ++i)
	{
		struct pawn *pawn = battle->player_pawns[player].data[i];

		if (pawn->slot->location == region)
		{
			column = offset_defend++;
		}
		else for(j = 0; j < NEIGHBORS_LIMIT; ++j)
		{
			if (pawn->slot->location == region->neighbors[j])
			{
				column = offset_attack[j]++;
				break;
			}
		}

		const struct point *positions = formation_positions(pawn->slot, region);
		pawn->moves[0].location = positions[column];
		// TODO pawn->moves[0].time = 0.0;
	}

	return 0;
}

int input_formation(const struct game *restrict game, const struct region *restrict region, const struct battle *restrict battle, unsigned char player)
{
	if_set(battle->field, battle);

	struct area areas[] = {
		{
			.left = 0,
			.right = SCREEN_WIDTH - 1,
			.top = 0,
			.bottom = SCREEN_HEIGHT - 1,
			.callback = input_none
		},
		{
			.left = 0,
			.right = BATTLEFIELD_WIDTH * FIELD_SIZE - 1,
			.top = 0,
			.bottom = BATTLEFIELD_HEIGHT * FIELD_SIZE - 1,
			.callback = input_place
		},
		{
			.left = CTRL_X,
			.right = CTRL_X + 6 * (FIELD_SIZE + 1) + FIELD_SIZE - 1, // TODO fix this
			.top = CTRL_Y,
			.bottom = CTRL_Y + 8 * (FIELD_SIZE + MARGIN) + FIELD_SIZE - 1, // TODO fix this
			.callback = input_pawn
		},
	};

	state.player = player;

	state.selected.pawn = battle->player_pawns[player].data[0];

	state.region = region - game->regions;

	return input_local(if_formation, areas, sizeof(areas) / sizeof(*areas), game);
}

static inline unsigned long timediff(const struct timeval *restrict end, const struct timeval *restrict start)
{
	return (end->tv_sec * 1000000 + end->tv_usec - start->tv_sec * 1000000 - start->tv_usec);
}

// TODO write this better
int input_animation(const struct game *restrict game, const struct battle *restrict battle)
{
	if_set(battle->field, battle);

	struct timeval start, now;
	double progress;
	gettimeofday(&start, 0);
	do
	{
		gettimeofday(&now, 0);
		progress = timediff(&now, &start) / (ANIMATION_DURATION * 1000000.0);
		if (if_animation(game->players, &state, game, progress))
			break;
	} while (progress < 1.0);

	return INPUT_DONE;

	//return input_local(if_animation, areas, sizeof(areas) / sizeof(*areas), game);
}
