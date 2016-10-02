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

#include <assert.h>
#include <errno.h>
#include <poll.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <unistd.h>

#include "errors.h"
#include "draw.h"
#include "game.h"
#include "map.h"
#include "pathfinding.h"
#include "movement.h"
#include "battle.h"
#include "combat.h"
#include "computer_map.h"
#include "computer_battle.h"
#include "input_map.h"
#include "input_report.h"
#include "input_battle.h"
#include "players.h"

// TODO can a read()/write() call be interrupted by a signal?
// TODO assertion for PIPE_BUF
// TODO is uint32_t available everywhere

struct request_generic
{
	enum request_type {REQUEST_MAP = 1, REQUEST_INVASION, REQUEST_FORMATION, REQUEST_BATTLE} type;
	unsigned char player;
	struct game *game;
};

struct request_map
{
	struct request_generic request;
};

struct request_invasion
{
	struct request_generic request;
	struct region *region;
};

struct request_formation
{
	struct request_generic request;
	struct battle *battle;
};

struct request_battle
{
	struct request_generic request;
	struct battle *battle;
	const struct obstacles *restrict obstacles;
	struct adjacency_list *restrict graph;
}; 

union request
{
	struct request_generic generic;

	struct request_map map;
	struct request_invasion invasion;
	struct request_formation formation;
	struct request_battle battle;
};

struct response
{
	int status;
	unsigned char player;
};

// 3 structs have members that can be modified by the owner of the given entity (possibly in a thread for that owner):
// struct region				(modifiable by region->owner)
// struct region . garrison		(modifiable by region->garrison.owner)
// struct troop					(modifiable by troop->owner)
// struct pawn					(modifiable by pawn->troop->owner)

/*static bool writeall(int fd, const void *restrict buffer, size_t total)
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
}*/

static int request_read(int input, union request *restrict request)
{
	static const size_t request_size[] =
	{
		[REQUEST_MAP] = sizeof(struct request_map),
		[REQUEST_INVASION] = sizeof(struct request_invasion),
		[REQUEST_FORMATION] = sizeof(struct request_formation),
		[REQUEST_BATTLE] = sizeof(struct request_battle),
	};
	size_t read_size;
	int status;

	status = read(input, request, sizeof(request->generic.type));
	if (!status)
		return ERROR_CANCEL;
	assert(status == sizeof(request->generic.type));

	read_size = request_size[request->generic.type] - sizeof(request->generic.type);
	assert(read(input, (unsigned char *)request + sizeof(request->generic.type), read_size) == read_size);

	return 0;
}

static void *computer_main(void *argument)
{
	struct player_info *info = argument;

	while (1)
	{
		union request request;
		struct response response;
		int status;

		if (request_read(info->in, &request) < 0)
			break;

		switch (request.generic.type)
		{
		case REQUEST_MAP:
			response.status = computer_map(request.generic.game, request.generic.player);
			break;

		case REQUEST_INVASION:
			response.status = computer_invasion(request.generic.game, request.generic.player, request.invasion.region);
			break;

		case REQUEST_FORMATION:
			response.status = computer_formation(request.generic.game, request.formation.battle, request.generic.player);
			break;

		case REQUEST_BATTLE:
			response.status = computer_battle(request.generic.game, request.battle.battle, request.generic.player, request.battle.graph, request.battle.obstacles);
			break;
		}

		response.player = request.generic.player;
		status = write(info->out, &response, sizeof(response));
		if (status < 0)
		{
			assert(errno == EPIPE);
			break;
		}
		else assert(status == sizeof(response));

		// Indicate that the player is ready.
		pthread_mutex_lock(&request.generic.game->mutex_input);
		request.generic.game->input_ready |= (1 << response.player);
		pthread_mutex_unlock(&request.generic.game->mutex_input);
	}

	close(info->in);
	close(info->out);
	return 0;
}

static int player_init(struct player *restrict player, void *(*main)(void *))
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

	if (pthread_create(&thread_id, 0, main, (void *)&player->info))
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

int players_init(struct game *restrict game)
{
	if (pthread_mutex_init(&game->mutex_input, 0))
		return ERROR_MEMORY;

	game->players_local_count = 0;
	for(size_t player = 0; player < game->players_count; player += 1)
	{
		switch (game->players[player].type)
		{
			int status;

		case Local:
			game->players_local[game->players_local_count++] = player;
			break;

		case Neutral:
		case Computer:
			status = player_init(game->players + player, &computer_main);
			if (status < 0)
			{
				for(size_t i = 0; i < player; i += 1)
					switch (game->players[i].type)
					{
					case Neutral:
					case Computer:
						close(game->players[i].control.in);
						close(game->players[i].control.out);
						break;
					}
				return status;
			}
			break;
		}
	}

	return 0;
}

int players_term(struct game *restrict game)
{
	for(size_t player = 0; player < game->players_count; player += 1)
		switch (game->players[player].type)
		{
		case Neutral:
		case Computer:
			close(game->players[player].control.in);
			close(game->players[player].control.out);
			break;
		}

	return 0;
}

static int players_wait(struct pollfd *restrict ready, size_t ready_count, struct game *restrict game)
{
	uint32_t input_processed;

	pthread_mutex_lock(&game->mutex_input);
	input_processed = game->input_ready;
	pthread_mutex_unlock(&game->mutex_input);

	while (input_processed != game->input_all)
	{
		assert(poll(ready, ready_count, -1) > 0);

		for(size_t i = 0; i < ready_count; ++i)
		{
			struct response response;

			if (!ready[i].revents)
				continue;

			assert(ready[i].revents == POLLIN);
			ready[i].revents = 0;

			assert(read(ready[i].fd, &response, sizeof(response)) == sizeof(response));
			if (response.status)
				return response.status;

			input_processed |= (1 << response.player);
		}
	}

	return 0;
}

int players_map(struct game *restrict game)
{
	struct pollfd ready[PLAYERS_LIMIT];
	size_t ready_count = 0;
	size_t player;
	int status;

	game->input_ready = 0;
	game->input_all = 0;

	for(player = 0; player < game->players_count; ++player)
	{
		struct request_map parameters = {.request.type = REQUEST_MAP, .request.player = player, .request.game = game};

		switch (game->players[player].type)
		{
		case Computer:
			assert(write(game->players[player].control.out, &parameters, sizeof(parameters)));

			ready[ready_count].fd = game->players[player].control.in;
			ready[ready_count].events = POLLIN;
			ready[ready_count].revents = 0;
			ready_count += 1;

			break;

		case Neutral:
			pthread_mutex_lock(&game->mutex_input);
			game->input_ready |= (1 << player);
			pthread_mutex_unlock(&game->mutex_input);
			break;
		}

		game->input_all |= (1 << player);
	}

	for(size_t i = 0; i < game->players_local_count; ++i)
	{
		player = game->players_local[i];

		status = input_map(game, player);
		if (status < 0)
			return status;

		pthread_mutex_lock(&game->mutex_input);
		game->input_ready |= (1 << player);
		pthread_mutex_unlock(&game->mutex_input);
	}

	return players_wait(ready, ready_count, game);
}

int players_invasion(struct game *restrict game, struct region *restrict region)
{
	int status;

	game->input_ready = 0;
	game->input_all = 0;

	switch (game->players[region->garrison.owner].type)
	{
	case Local:
		{
			status = input_report_invasion(game, region->garrison.owner, region);
			if (!status)
			{
				pthread_mutex_lock(&game->mutex_input);
				game->input_ready |= (1 << region->garrison.owner);
				pthread_mutex_unlock(&game->mutex_input);
			}
			return status;
		}

	case Computer:
		{
			struct pollfd ready;
			struct request_invasion parameters = {.request.type = REQUEST_INVASION, .request.player = region->garrison.owner, .request.game = game, .region = region};

			assert(write(game->players[region->garrison.owner].control.out, &parameters, sizeof(parameters)));

			ready.fd = game->players[region->garrison.owner].control.in;
			ready.events = POLLIN;
			ready.revents = 0;
			return players_wait(&ready, 1, game);
		}
	}

	return 0;
}

int players_formation(struct game *restrict game, struct battle *restrict battle, int hotseat)
{
	struct pollfd ready[PLAYERS_LIMIT];
	size_t ready_count = 0;
	size_t player;
	int status;

	game->input_ready = 0;
	game->input_all = 0;

	for(player = 0; player < game->players_count; ++player)
	{
		struct request_formation parameters = {.request.type = REQUEST_FORMATION, .request.player = player, .request.game = game, .battle = battle};

		if (!battle->players[player].alive)
			continue;

		switch (game->players[player].type)
		{
		case Computer:
			assert(write(game->players[player].control.out, &parameters, sizeof(parameters)));
			ready[ready_count].fd = game->players[player].control.in;
			ready[ready_count].events = POLLIN;
			ready[ready_count].revents = 0;
			ready_count += 1;
			break;
		}

		game->input_all |= (1 << player);
	}

	for(size_t i = 0; i < game->players_local_count; ++i)
	{
		player = game->players_local[i];
		if (!battle->players[player].alive)
			continue;

		status = input_formation(game, battle, player, hotseat);
		if (status < 0)
			return status;

		pthread_mutex_lock(&game->mutex_input);
		game->input_ready |= (1 << player);
		pthread_mutex_unlock(&game->mutex_input);
	}

	return players_wait(ready, ready_count, game);
}

int players_battle(struct game *restrict game, struct battle *restrict battle, const struct obstacles *restrict obstacles[static PLAYERS_LIMIT], struct adjacency_list *restrict graph[static PLAYERS_LIMIT])
{
	struct pollfd ready[PLAYERS_LIMIT];
	size_t ready_count = 0;
	size_t player;
	int status;

	game->input_ready = 0;
	game->input_all = 0;

	for(player = 0; player < game->players_count; ++player)
	{
		struct request_battle parameters = {.request.type = REQUEST_FORMATION, .request.player = player, .request.game = game, .battle = battle};
		parameters.obstacles = obstacles[game->players[player].alliance];
		parameters.graph = graph[player];

		if (!battle->players[player].alive)
			continue;

		switch (game->players[player].type)
		{
		case Neutral:
		case Computer:
			assert(write(game->players[player].control.out, &parameters, sizeof(parameters)));

			ready[ready_count].fd = game->players[player].control.in;
			ready[ready_count].events = POLLIN;
			ready[ready_count].revents = 0;
			ready_count += 1;

			break;
		}

		game->input_all |= (1 << player);
	}

	for(size_t i = 0; i < game->players_local_count; ++i)
	{
		player = game->players_local[i];
		if (!battle->players[player].alive)
			continue;

		status = input_battle(game, battle, player, graph[player], obstacles[game->players[player].alliance]);
		if (status < 0)
			return status;

		pthread_mutex_lock(&game->mutex_input);
		game->input_ready |= (1 << player);
		pthread_mutex_unlock(&game->mutex_input);
	}

	return players_wait(ready, ready_count, game);
}
