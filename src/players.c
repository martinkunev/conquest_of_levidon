/*
 * Conquest of Levidon
 * Copyright (C) 2016  Martin Kunev <martinkunev@gmail.com>
 *
 * This file is part of Conquest of Levidon.
 *
 * Conquest of Levidon is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation version 3 of the License.
 *
 * Conquest of Levidon is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with Conquest of Levidon.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <stdbool.h>
#include <stdlib.h>
#include <unistd.h>

#include "errors.h"
#include "draw.h"
#include "game.h"
#include "map.h"
#include "pathfinding.h"
#include "movement.h"
#include "battle.h"
#include "computer_map.h"
#include "computer_battle.h"
#include "players.h"

// 3 structs have members that can be modified by the owner of the given entity (possibly in a thread for that owner):
// struct region	(modifiable by region->owner)
// struct troop		(modifiable by troop->owner)
// struct pawn		(modifiable by pawn->troop->owner)

static bool writeall(int fd, const void *restrict buffer, size_t total)
{
	ssize_t size;
	for(size_t offset = 0; offset < total; offset += size)
	{
		size = write(fd, (const unsigned char *)buffer + offset, total - offset);
		if (size < 0) return false;
	}
	return true;
}

static bool readall(int fd, void *restrict buffer, size_t total)
{
	ssize_t size;
	for(size_t offset = 0; offset < total; offset += size)
	{
		size = read(fd, (unsigned char *)buffer + offset, total - offset);
		if (size <= 0) return false;
	}
	return true;
}

static void *computer_main(void *argument)
{
	struct player_info *info = argument;

	while (1)
	{
		unsigned char request;
		int status;

		status = read(info->in, &request, sizeof(request));
		if (status == 0)
		{
			close(info->in);
			close(info->out);
			return 0;
		}
		if (status != sizeof(request)) abort();

		switch (request)
		{
		case REQUEST_MAP:
			{
				struct request_map parameters;
				if (!readall(info->in, &parameters, sizeof(parameters)))
					abort();
				status = computer_map(parameters.game, info->player);
			}
			break;

		case REQUEST_INVASION:
			{
				struct request_invasion parameters;
				if (!readall(info->in, &parameters, sizeof(parameters)))
					abort();
				status = computer_invasion(parameters.game, parameters.region, info->player);
			}
			break;

		case REQUEST_FORMATION:
			{
				struct request_formation parameters;
				if (!readall(info->in, &parameters, sizeof(parameters)))
					abort();
				status = computer_formation(parameters.game, parameters.battle, info->player);
			}
			break;

		case REQUEST_BATTLE:
			{
				struct request_battle parameters;
				if (!readall(info->in, &parameters, sizeof(parameters)))
					abort();
				status = computer_battle(parameters.game, parameters.battle, info->player, parameters.graph, parameters.obstacles);
			}
			break;
		}

		if (!writeall(info->out, &status, sizeof(status)))
			abort();
	}
}

int player_init(struct player *restrict player, unsigned char id)
{
	int io[2];
	pthread_t thread_id;

	// Create communication channel from main thread to worker thread.
	if (pipe(io) < 0)
		return ERROR_MEMORY;
	player->control.in = io[0];
	player->info.out = io[1];

	// Create communication channel from worker thread to main thread.
	if (pipe(io) < 0)
	{
		close(io[0]);
		close(io[1]);
		return ERROR_MEMORY;
	}
	player->info.in = io[0];
	player->control.out = io[1];

	player->info.player = id;

	if (pthread_create(&thread_id, 0, &computer_main, (void *)&player->info))
	{
		close(player->control.in);
		close(player->control.out);
		close(player->info.in);
		close(player->info.out);
		return ERROR_MEMORY;
	}
	pthread_detach(thread_id);

	return 0;
}

void player_map(const struct game *restrict game, unsigned char player)
{
	struct request_map parameters = {.game = game};
	if (!writeall(game->players[player].control.out, &parameters, sizeof(parameters)))
		abort();
}

void player_invasion(const struct game *restrict game, const struct region *restrict region, unsigned char player)
{
	struct request_invasion parameters = {.game = game, .region = region};
	if (!writeall(game->players[player].control.out, &parameters, sizeof(parameters)))
		abort();
}

void player_formation(const struct game *restrict game, const struct battle *restrict battle, unsigned char player)
{
	struct request_formation parameters = {.game = game, .battle = battle};
	if (!writeall(game->players[player].control.out, &parameters, sizeof(parameters)))
		abort();
}

void player_battle(const struct game *restrict game, const struct battle *restrict battle, unsigned char player, struct adjacency_list *restrict graph, const struct obstacles *restrict obstacles)
{
	struct request_battle parameters = {.game = game, .battle = battle, .graph = graph, .obstacles = obstacles};
	if (!writeall(game->players[player].control.out, &parameters, sizeof(parameters)))
		abort();
}

int player_response(const struct game *restrict game, unsigned char player)
{
	int status;
	if (!readall(game->players[player].control.in, &status, sizeof(status)))
		abort();
	return status;
}

int player_term(struct player *restrict player)
{
	close(player->control.in);
	close(player->control.out);
	return 0;
}
