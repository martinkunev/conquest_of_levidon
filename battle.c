#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "types.h"
#include "heap.h"
#include "battle.h"
#include "interface.h"

// Alliance number must be less than the number of players.

// Constraints necessary to ensure that no overflow can happen:
// x < 32
// y < 32
// t <= 8

// damage depends on whether the other side is fighting (fleeing will lead to more damage received)
// When escaping from enemy, unit speed is reduced by 1.

// When shooting, each pawn receives the amount of damage of the shooting (the more pawns on the field, the more damage is dealt).

// player 0 is the neutral player

/*
Too many units on a single field don't deal the full amount of damage.

The damage coefficient when the units are more than n is described by the function:
f(x) where:
	f(n) = 1
	f(2n) = 0.5
	lim(x->oo) f(x) = 0
	f'(n) = 0
	f"(2n) = 0
*/

#define DIAMETER 1

struct encounter
{
	double moment;
	unsigned pawns[2];
};

#include <stdio.h>

/*static unsigned long gcd(unsigned long a, unsigned long b)
{
	// asert((a != 0) || (b != 0));
	unsigned long c;
	while (b)
	{
		c = a % b;
		a = b;
		b = c;
	}
	return a;
}

static unsigned deaths(struct pawn *restrict pawn)
{
	// TODO calculate deaths and set them in pawn

	unsigned long n, k;

	unsigned long result = 1;

	unsigned long d;

	// (n.h m)

	// pawn->slot->count * 

	unsigned long chance = ((unsigned long)(random() % 65536) << 48) | ((unsigned long)(random() % 65536) << 32) | ((unsigned long)(random() % 65536) << 16) |= (unsigned long)(random() % 65536);

	unsigned long i;
	for(i = 1; i <= k; ++i)
	{
		d = gcd(result, i);
		result = (result / d) * ((n - i + 1) / (i / d));
	}

	return 0;
}*/

int reachable(const struct player *restrict players, struct pawn *battlefield[BATTLEFIELD_HEIGHT][BATTLEFIELD_WIDTH], const struct pawn *restrict pawn, unsigned char x, unsigned char y)
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
}

int shootable(const struct player *restrict players, struct pawn *battlefield[BATTLEFIELD_HEIGHT][BATTLEFIELD_WIDTH], const struct pawn *restrict pawn, unsigned char x, unsigned char y)
{
	// Only ranged units can shoot.
	if (!pawn->slot->unit->shoot) return 0;

	// The pawn cannot shoot if there's an enemy on the same field.
	unsigned char alliance = players[pawn->slot->player].alliance;
	const struct pawn *item = battlefield[pawn->move.y[0]][pawn->move.x[0]];
	do if (players[item->slot->player].alliance != alliance) return 0;
	while (item = item->_next);

	// Determine the euclidean distance between the two fields. Round the value to integer.
	int dx = x - pawn->move.x[0], dy = y - pawn->move.y[0];
	unsigned distance = round(sqrt(dx * dx + dy * dy));

	return (distance <= pawn->slot->unit->range);
}

static void pawn_remove(struct pawn *battlefield[BATTLEFIELD_HEIGHT][BATTLEFIELD_WIDTH], struct pawn *pawn)
{
	if (pawn->_prev) pawn->_prev->_next = pawn->_next;
	else battlefield[pawn->move.y[0]][pawn->move.x[0]] = pawn->_next;
	if (pawn->_next) pawn->_next->_prev = pawn->_prev;
}

static void pawn_move(struct pawn *battlefield[BATTLEFIELD_HEIGHT][BATTLEFIELD_WIDTH], struct pawn *pawn, unsigned char x, unsigned char y)
{
	pawn_remove(battlefield, pawn);

	pawn->_prev = 0;
	pawn->_next = 0;

	// Attach the pawn to its new position.
	struct pawn **field = &battlefield[y][x];
	while (*field)
	{
		pawn->_prev = *field;
		field = &(*field)->_next;
	}
	*field = pawn;

	// Set pawn position.
	pawn->move.x[1] = pawn->move.x[0] = x;
	pawn->move.y[1] = pawn->move.y[0] = y;
	pawn->move.t[0] = 0;
	pawn->move.t[1] = 8;
}

// Determines whether the two objects will collide. Returns the time of the collision.
static double battle_encounter(const struct move *restrict o0, const struct move *restrict o1)
{
	// TODO support movements with different start times
	// assert(o0->t[0] == o1->t[0]);

	// Calculate time differences.
	double o0_t = o0->t[1] - o0->t[0], o1_t = o1->t[1] - o1->t[0];

	// Find the coefficients of the distance^2 function:
	// f(t) = (a * t^2 + b * t + c) / d

	double v_x = (o1->x[1] - o1->x[0]) * o0_t - (o0->x[1] - o0->x[0]) * o1_t;
	double v_y = (o1->y[1] - o1->y[0]) * o0_t - (o0->y[1] - o0->y[0]) * o1_t;

	double d_x = (o1->x[0] * o1->t[1] - o1->x[1] * o1->t[0]) * o0_t - (o0->x[0] * o0->t[1] - o0->x[1] * o0->t[0]) * o1_t;
	double d_y = (o1->y[0] * o1->t[1] - o1->y[1] * o1->t[0]) * o0_t - (o0->y[0] * o0->t[1] - o0->y[1] * o0->t[0]) * o1_t;

	double a = v_x * v_x + v_y * v_y; // a >= 0
	double b = 2 * (d_x * v_x + d_y * v_y);
	double c = d_x * d_x + d_y * d_y;

	// f'(t) = (2 * a * t + b) / d
	// f"(t) = (2 * a) / d

	if (!a) return NAN; // if a == 0 then b == 0 and so f'(t) = 0 (constant distance; no encounter)

	// Calculate demoninator.
	double d = o1_t * o0_t; // d >= 0

	// Consider only the time interval when both pawns move.
	double t = ((o0->t[1] <= o1->t[1]) ? o0->t[1] : o1->t[1]);

	double moment, distance;

	// Find the minimum of the square of the distance.
	// It is reached at time -(b / (2 * a)).
	moment = -((double)b / (2 * a));

	// Check if the pawns encounter when both of them are moving [o0->t[0], t].
	if (moment <= o0->t[0]) return NAN; // the objects go away from each other; no encounter
	else if (moment <= t)
	{
		// distance^2 = (c - (double)(b * b) / (4 * a)) / (d * d)
		if ((4 * a * c - b * b) < (4 * a * d * d * DIAMETER * DIAMETER)) return moment;
	}
	else
	{
		// distance^2 = (a * t * t + b * t + c) / (d * d)
		if ((a * t * t + b * t + c) < (d * d * DIAMETER * DIAMETER)) return t;
	}

	// Check if the pawns encounter after one of them stopped moving.

	struct move o0_after, o1_after;

	// Set movement parameters of the two pawns.
	// The movement of one of the pawns remains unchanged while the other pawn stays at a constant position.
	if (o1->t[1] < o0->t[1])
	{
		o0_after = *o0;
		o1_after = (struct move){{o1->x[1], o1->x[1]}, {o1->y[1], o1->y[1]}, {o1->t[1], o0->t[1]}};
	}
	else if (o0->t[1] < o1->t[1])
	{
		o0_after = (struct move){{o0->x[1], o0->x[1]}, {o0->y[1], o0->y[1]}, {o0->t[1], o1->t[1]}};
		o1_after = *o1;
	}
	else return NAN;

	// Find the coefficients of the distance^2 function:
	// f(t) = (a * t^2 + b * t + c) / d

	v_x = (o1_after.x[1] - o1_after.x[0]) * o0_t - (o0_after.x[1] - o0_after.x[0]) * o1_t;
	v_y = (o1_after.y[1] - o1_after.y[0]) * o0_t - (o0_after.y[1] - o0_after.y[0]) * o1_t;

	d_x = (o1_after.x[0] * o1_after.t[1] - o1_after.x[1] * o1_after.t[0]) * o0_t - (o0_after.x[0] * o0_after.t[1] - o0_after.x[1] * o0_after.t[0]) * o1_t;
	d_y = (o1_after.y[0] * o1_after.t[1] - o1_after.y[1] * o1_after.t[0]) * o0_t - (o0_after.y[0] * o0_after.t[1] - o0_after.y[1] * o0_after.t[0]) * o1_t;

	a = v_x * v_x + v_y * v_y; // a >= 0
	b = 2 * (d_x * v_x + d_y * v_y);
	c = d_x * d_x + d_y * d_y;

	// f'(t) = (2 * a * t + b) / d
	// f"(t) = (2 * a) / d

	if (!a) return NAN; // if a == 0 then b == 0 and so f'(t) = 0 (constant distance; no encounter)

	// Find the minimum of the square of the distance.
	// It is reached at time -(b / (2 * a)).
	moment = -((double)b / (2 * a));

	if (moment <= t) return NAN; // the distance at moment t was checked in the previous case
	else if (moment <= o0_after.t[1])
	{
		// distance^2 = (c - (double)(b * b) / (4 * a)) / (d * d)
		if ((4 * a * c - b * b) < (4 * a * d * d * DIAMETER * DIAMETER)) return moment;
	}
	else
	{
		t = o0_after.t[1];
		// distance^2 = (a * t * t + b * t + c) / (d * d)
		if ((a * t * t + b * t + c) < (d * d * DIAMETER * DIAMETER)) return t;
	}

	return NAN;
}

// TODO support different start times
static void location(const struct move *restrict o0, const struct move *restrict o1, double moment, double *restrict x, double *restrict y)
{
	// Calculate time differences.
	int o0_t = o0->t[1] - o0->t[0], o1_t = o1->t[1] - o1->t[0];

	// Calculate the positions of the objects when they are closest to each other.
	// Use the parametric equations of the lines of the two movements for the calculation.

	double x0, y0, x1, y1;
	double arg;

	arg = ((moment > o0->t[1]) ? o0->t[1] : moment);
	x0 = ((o0->x[0] * o0->t[1] - o0->x[1] * o0->t[0]) + (o0->x[1] - o0->x[0]) * arg) / o0_t;
	y0 = ((o0->y[0] * o0->t[1] - o0->y[1] * o0->t[0]) + (o0->y[1] - o0->y[0]) * arg) / o0_t;

	arg = ((moment > o1->t[1]) ? o1->t[1] : moment);
	x1 = ((o1->x[0] * o1->t[1] - o1->x[1] * o1->t[0]) + (o1->x[1] - o1->x[0]) * arg) / o1_t;
	y1 = ((o1->y[0] * o1->t[1] - o1->y[1] * o1->t[0]) + (o1->y[1] - o1->y[0]) * arg) / o1_t;

	// Find the point in the middle of (x0, y0) and (x1, y1).
	*x = (x0 + x1) / 2;
	*y = (y0 + y1) / 2;
}

// Calculates the position of a pawn at a given moment.
static void position(const struct move *restrict o, double moment, double *restrict x, double *restrict y)
{
	int o_t = o->t[1] - o->t[0];
	*x = ((o->x[0] * o->t[1] - o->x[1] * o->t[0]) + (o->x[1] - o->x[0]) * moment) / o_t;
	*y = ((o->y[0] * o->t[1] - o->y[1] * o->t[0]) + (o->y[1] - o->y[0]) * moment) / o_t;
}

static void battle_fight(const struct player *restrict players, size_t players_count, const struct pawn *restrict pawn, unsigned *restrict count, unsigned *restrict damage)
{
	memset(count, 0, players_count * sizeof(*damage));
	memset(damage, 0, players_count * sizeof(*damage));

	while (pawn)
	{
		count[players[pawn->slot->player].alliance] += pawn->slot->count;
		damage[players[pawn->slot->player].alliance] += pawn->slot->count * pawn->slot->unit->damage;

		pawn = pawn->_next;
	}

	// Precalculate the sum of the values for the first n alliances in the corresponding array element.
	size_t i;
	for(i = 1; i < players_count; ++i)
	{
		count[i] += count[i - 1];
		damage[i] += damage[i - 1];
	}
}

static void battle_damage(struct pawn *battlefield[BATTLEFIELD_HEIGHT][BATTLEFIELD_WIDTH], struct pawn *restrict pawn, unsigned damage)
{
	unsigned health = pawn->slot->unit->health;

	pawn->hurt += damage;

	if (pawn->hurt >= (pawn->slot->count * health))
	{
		pawn->slot->count = 0;
		pawn_remove(battlefield, pawn);
	}
	else
	{
		// TODO this is a stupid implementation. find a better one

		// Deal damage by performing consecutive hits to a random unit of the pawn.
		// Check for death after each hit.
		unsigned char hits[SLOT_COUNT_MAX] = {0};
		size_t index;
		for(damage = 0; damage < pawn->hurt; ++damage)
		{
			index = random() % pawn->slot->count;
			if (++hits[index] == health)
			{
				hits[index] = hits[--pawn->slot->count];
				damage -= health;
				pawn->hurt -= health;
			}
		}
	}
}

static int battle_round(const struct player *restrict players, struct pawn *battlefield[BATTLEFIELD_HEIGHT][BATTLEFIELD_WIDTH], struct pawn *pawns, size_t pawns_count, struct heap *collisions)
{
	size_t i, j;

	struct encounter *encounter, *item;

	// Look for pawns that will encounter each other.
	double moment, distance;
	for(i = 0; i < (pawns_count - 1); ++i)
	{
		for(j = i + 1; j < pawns_count; ++j)
		{
			// Ignore encounters if the two pawns are not enemies to each other.
			if (players[pawns[i].slot->player].alliance == players[pawns[j].slot->player].alliance) continue;

			// Nothing to do if neither of the pawns will move.
			if ((pawns[i].move.x[1] == pawns[i].move.x[0]) && (pawns[i].move.y[1] == pawns[i].move.y[0]) && (pawns[j].move.x[1] == pawns[j].move.x[0]) && (pawns[j].move.y[1] == pawns[j].move.y[0]))
				continue;

			moment = battle_encounter(&pawns[i].move, &pawns[j].move);
			if (!isnan(moment))
			{
				encounter = malloc(sizeof(*encounter));
				if (!encounter) return -1;

				encounter->moment = moment;
				encounter->pawns[0] = i;
				encounter->pawns[1] = j;
				if (!heap_push(collisions, (double *)encounter)) return -1;
			}
		}
	}

	int x, y;
	double real_x, real_y;

	unsigned *index;

	// Process each encounter in chronological order.
	// Look if the encounter prevents a later encounter or causes another encounter.
	while (collisions->count)
	{
		item = (struct encounter *)heap_front(collisions);
		heap_pop(collisions);

		index = item->pawns;

		location(&pawns[index[0]].move, &pawns[index[1]].move, item->moment, &real_x, &real_y);

		// Use the closest field for location of the encounter.
		// If the real location is in the middle of two fields, choose one randomly.
		if (fabs(real_x - floor(real_x) - 0.5) >= APPROX_ERROR) x = (int)(real_x + 0.5);
		else x = real_x + ((random() % 2) ? 0.5 : 0);
		if (fabs(real_y - floor(real_y) - 0.5) >= APPROX_ERROR) y = (int)(real_y + 0.5);
		else y = real_y + ((random() % 2) ? 0.5 : 0);

		if (item->moment < pawns[index[0]].move.t[1]) // index[0] is still moving
		{
			if (item->moment < pawns[index[1]].move.t[1]) // index[1] is still moving
			{
				// Move both pawns.

				pawns[index[0]].move.x[1] = x;
				pawns[index[0]].move.y[1] = y;
				pawns[index[0]].move.t[1] = item->moment;

				pawns[index[1]].move.x[1] = x;
				pawns[index[1]].move.y[1] = y;
				pawns[index[1]].move.t[1] = item->moment;

				// The stop of the pawns can lead to new encounters. Look for new encounters with each non-moved pawn.
				for(i = 0; i < pawns_count; ++i)
				{
					if (item->moment >= pawns[i].move.t[1]) continue; // i is not moving

					// Ignore encounters if the two pawns are not enemies to each other.
					if (players[pawns[i].slot->player].alliance != players[pawns[index[0]].slot->player].alliance)
					{
						moment = battle_encounter(&pawns[i].move, &pawns[index[0]].move);
						if (!isnan(moment))
						{
							encounter = malloc(sizeof(*encounter));
							if (!encounter) return -1;

							encounter->moment = moment;
							encounter->pawns[0] = i;
							encounter->pawns[1] = index[0];
							if (!heap_push(collisions, (double *)encounter)) return -1;
						}
					}
					else if (players[pawns[i].slot->player].alliance != players[pawns[index[1]].slot->player].alliance)
					{
						moment = battle_encounter(&pawns[i].move, &pawns[index[1]].move);
						if (!isnan(moment))
						{
							encounter = malloc(sizeof(*encounter));
							if (!encounter) return -1;

							encounter->moment = moment;
							encounter->pawns[0] = i;
							encounter->pawns[1] = index[1];
							if (!heap_push(collisions, (double *)encounter)) return -1;
						}
					}
				}
			}
			else
			{
				// Pawn 1 is already moved (which means it stopped moving before this encounter).
				// The encounter will take place if Pawn 0 goes through the location of Pawn 1.
				if ((x == pawns[index[1]].move.x[1]) && (y == pawns[index[1]].move.y[1]))
				{
					pawns[index[0]].move.x[1] = x;
					pawns[index[0]].move.y[1] = y;
					pawns[index[0]].move.t[1] = item->moment;

					// The stop of the pawn can lead to new encounters. Look for new encounters with each non-moved pawn.
					for(i = 0; i < pawns_count; ++i)
					{
						if (item->moment >= pawns[i].move.t[1]) continue; // i is not moving

						// Ignore encounters if the two pawns are not enemies to each other.
						if (players[pawns[i].slot->player].alliance == players[pawns[index[0]].slot->player].alliance) continue;

						moment = battle_encounter(&pawns[i].move, &pawns[index[0]].move);
						if (!isnan(moment))
						{
							encounter = malloc(sizeof(*encounter));
							if (!encounter) return -1;

							encounter->moment = moment;
							encounter->pawns[0] = i;
							encounter->pawns[1] = index[0];
							if (!heap_push(collisions, (double *)encounter)) return -1;
						}
					}
				}
			}
		}
		else if (item->moment < pawns[index[1]].move.t[1]) // index[1] is still moving
		{
			// Pawn 0 is already moved (which means it stopped moving before this encounter).
			// The encounter will take place if Pawn 1 goes through the location of Pawn 0.
			if ((x == pawns[index[0]].move.x[1]) && (y == pawns[index[0]].move.y[1]))
			{
				pawns[index[1]].move.x[1] = x;
				pawns[index[1]].move.y[1] = y;
				pawns[index[1]].move.t[1] = item->moment;

				// The stop of the pawn can lead to new encounters. Look for new encounters with each non-moved pawn.
				for(i = 0; i < pawns_count; ++i)
				{
					if (item->moment >= pawns[i].move.t[1]) continue; // i is not moving

					// Ignore encounters if the two pawns are not enemies to each other.
					if (players[pawns[i].slot->player].alliance == players[pawns[index[1]].slot->player].alliance) continue;

					moment = battle_encounter(&pawns[i].move, &pawns[index[1]].move);
					if (!isnan(moment))
					{
						encounter = malloc(sizeof(*encounter));
						if (!encounter) return -1;

						encounter->moment = moment;
						encounter->pawns[0] = i;
						encounter->pawns[1] = index[1];
						if (!heap_push(collisions, (double *)encounter)) return -1;
					}
				}
			}
		}

		free(item);
	}

	return 0;
}

static void battle_escape(const struct player *restrict players, size_t players_count, const struct pawn *restrict pawn, unsigned *restrict count, unsigned *restrict damage)
{
	memset(count, 0, players_count * sizeof(*damage));
	memset(damage, 0, players_count * sizeof(*damage));

	// The pawns staying on the same field deal damage.
	// The pawns moving to another field receive damage.
	while (pawn)
	{
		if ((pawn->move.x[1] == pawn->move.x[0]) && (pawn->move.y[1] == pawn->move.y[0]))
			damage[players[pawn->slot->player].alliance] += pawn->slot->count * pawn->slot->unit->damage;
		else
			count[players[pawn->slot->player].alliance] += pawn->slot->count;

		pawn = pawn->_next;
	}

	// Precalculate the sum of the values for the first n alliances in the corresponding array element.
	size_t i;
	for(i = 1; i < players_count; ++i)
	{
		count[i] += count[i - 1];
		damage[i] += damage[i - 1];
	}
}

static int battle_init(struct battle *restrict battle, const struct player *restrict players, size_t players_count, struct pawn *restrict pawns, size_t pawns_count)
{
	size_t i;

	battle->players = players;
	battle->players_count = players_count;

	battle->player_pawns = malloc(players_count * sizeof(struct vector));
	if (!battle->player_pawns) return -1;
	for(i = 0; i < players_count; ++i)
		if (!vector_init(battle->player_pawns + i))
			goto error;

	memset((void *)battle->field, 0, BATTLEFIELD_HEIGHT * BATTLEFIELD_WIDTH * sizeof(struct pawn *));

	// Put the pawns on the battlefield.
	// Add each pawn to an array of pawns for the corresponding player.
	unsigned char x, y;
	for(i = 0; i < pawns_count; ++i)
	{
		x = pawns[i].move.x[1] = pawns[i].move.x[0];
		y = pawns[i].move.y[1] = pawns[i].move.y[0];
		pawns[i].move.t[0] = 0;
		pawns[i].move.t[1] = 8;

		pawns[i]._prev = 0;
		if (battle->field[y][x])
		{
			battle->field[y][x]->_prev = pawns + i;
			pawns[i]._next = battle->field[y][x];
		}
		else pawns[i]._next = 0;
		battle->field[y][x] = pawns + i;

		pawns[i].hurt = 0;

		if (!vector_add(battle->player_pawns + pawns[i].slot->player, pawns + i)) goto error;
	}

	return 0;

error:
	// TODO free memory
	free(battle->player_pawns);
	return -1;
}

// Returns winner's alliance number or -1 on error.
static int battle_end(struct battle *restrict battle, unsigned char host)
{
	bool end = 1;
	bool alive;

	signed char winner = -1;
	unsigned char alliance;

	struct pawn *pawn;

	size_t i, j;
	for(i = 0; i < battle->players_count; ++i)
	{
		if (!battle->player_pawns[i].length) continue; // skip dead players

		alliance = battle->players[i].alliance;

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

	// If the battle is over, free the memory allocated in battle_init().
	if (end)
	{
		for(i = 0; i < battle->players_count; ++i)
			vector_term(battle->player_pawns + i);
		free(battle->player_pawns);
	}

	if (end) return ((winner >= 0) ? winner : host);
	else return -1;
}

int battle(const struct player *restrict players, size_t players_count, struct region *restrict region)
{
	struct slot *slot;
	struct pawn *pawns;
	size_t pawns_count = 0;

	size_t i, j;

	for(slot = region->slots; slot; slot = slot->_next)
		pawns_count += 1;
	pawns = malloc(pawns_count * sizeof(*pawns));
	if (!pawns) return -1;

	i = 0;
	for(slot = region->slots; slot; slot = slot->_next)
	{
		pawns[i]._prev = 0;
		pawns[i]._next = 0;
		pawns[i].slot = slot;
		pawns[i].hurt = 0;
		pawns[i].move = (struct move){.x = {i, i}, .y = {0, 0}, .t = {0, 8}};

		pawns[i].shoot.x = -1;
		pawns[i].shoot.y = -1;

		i += 1;
	}

	unsigned *count = 0, *damage = 0;
	struct pawn *pawn;
	unsigned char alliance;

	int status;

	unsigned char player;

	unsigned char a;
	unsigned c, d;

	int dx, dy;

	double t;
	unsigned target_index;
	double targets[3] = {0.5, 0.078125, 0.046875}; // 1/2, 5/64, 3/64
	double miss;
	double x, y;

	struct heap collisions;
	if (!heap_init(&collisions)) return -1;

	struct battle battle;
	if (battle_init(&battle, players, players_count, pawns, pawns_count) < 0)
	{
		heap_term(&collisions);
		return -1;
	}

	if_set(battle.field);

	count = malloc(players_count * sizeof(*count));
	if (!count)
	{
		status = -1;
		goto finally;
	}

	damage = malloc(players_count * sizeof(*damage));
	if (!damage)
	{
		free(count);
		status = -1;
		goto finally;
	}

	do
	{
		// Ask each player to perform battle actions.
		for(player = 1; player < players_count; ++player) // TODO skip player 0 in a natural way
		{
			if (!battle.player_pawns[player].length) continue; // skip dead players
			if (input_player(player, players) < 0)
			{
				status = -1;
				goto finally;
			}
		}

		// Deal damage to each pawn escaping from enemy pawns.
		for(i = 0; i < BATTLEFIELD_HEIGHT; ++i)
		{
			for(j = 0; j < BATTLEFIELD_WIDTH; ++j)
			{
				if (!battle.field[i][j]) continue;
				pawn = battle.field[i][j];

				battle_escape(players, players_count, pawn, count, damage);

				do
				{
					if ((pawn->move.x[1] == pawn->move.x[0]) && (pawn->move.y[1] == pawn->move.y[0]))
						continue;

					alliance = players[pawn->slot->player].alliance;

					d = 0;

					// Each alliance deals damage equally to each enemy unit.
					for(a = 0; a < players_count; ++a)
					{
						if (a == alliance) continue;

						// Calculate the number of enemy units for a.
						c = count[players_count - 1] - (count[a] - (a ? count[a - 1] : 0));

						d += (double)((damage[a] - (a ? damage[a - 1] : 0)) * pawn->slot->count) / c + 0.5;
					}

					// TODO implement the too many units sanction
					// d = ...;

					// Pawn takes more damage if it moves slower.
					dx = (pawn->move.x[1] - pawn->move.x[0]);
					dy = (pawn->move.y[1] - pawn->move.y[0]);
					d *= sqrt((pawn->move.t[1] - pawn->move.t[0]) / sqrt(dx * dx + dy * dy));

					// Deal damage and kill some of the units.
					if (d) battle_damage(battle.field, pawn, d);

					// TODO handle dead pawns
				} while (pawn = pawn->_next);
			}
		}

		// Handle pawn movement interference.
		status = battle_round(players, battle.field, pawns, pawns_count, &collisions);

		// Deal damage from the shooting pawns.
		for(i = 0; i < pawns_count; ++i)
		{
			if (!pawns[i].slot->count) continue; // skip dead pawns

			if ((pawns[i].shoot.x >= 0) && (pawns[i].shoot.y >= 0) && ((pawns[i].shoot.x != pawns[i].move.x[0]) || (pawns[i].shoot.y != pawns[i].move.y[0]))) // if the pawn is shooting
			{
				// Determine the time of the shooting.
				dx = pawns[i].shoot.x - pawns[i].move.x[0];
				dy = pawns[i].shoot.y - pawns[i].move.y[0];
				t = (miss * (pawns[i].move.t[1] - pawns[i].move.t[0]));

				// The accuracy of the shoting decreases with the distance.
				// when the pawn shoots at the distance equal to its range, miss == 0.5
				miss = sqrt(dx * dx + dy * dy) / (pawns[i].slot->unit->range * 2); // TODO can this become negative?

				// Deal damage to each pawn that is close enough to the shooting target.
				for(j = 0; j < pawns_count; ++j)
				{
					if (!pawns[j].slot->count) continue; // skip dead pawns

					if ((pawns[j].move.x[1] == pawns[j].move.x[0]) && (pawns[j].move.y[1] == pawns[j].move.y[0]))
					{
						// no need to calculate the position of non-moving pawns
						x = pawns[j].move.x[0];
						y = pawns[j].move.y[0];
					}
					else position(&pawns[j].move, t, &x, &y);
					target_index = (unsigned)(fabs(pawns[i].shoot.x - x) + 0.5) + (unsigned)(fabs(pawns[i].shoot.y - y) + 0.5);

					// Damage is dealt to the target field and to its neighbors.
					if (target_index < (sizeof(targets) / sizeof(*targets)))
					{
						d = pawns[i].slot->count * pawns[i].slot->unit->shoot * (1 - miss) * targets[target_index] + 0.5;
						pawns[j].hurt += d;

						// TODO deal more damage to moving pawns
					}
				}
			}
		}

		// Handle pawn movement and deaths from shooting.
		for(i = 0; i < pawns_count; ++i)
		{
			if (!pawns[i].slot->count) continue; // skip dead pawns

			battle_damage(battle.field, pawns + i, 0);

			pawns[i].shoot.x = -1;
			pawns[i].shoot.y = -1;

			if ((pawns[i].move.x[1] == pawns[i].move.x[0]) && (pawns[i].move.y[1] == pawns[i].move.y[0]))
				continue;

			pawn_move(battle.field, pawns + i, pawns[i].move.x[1], pawns[i].move.y[1]);
		}

		// Deal damage to enemy pawns located on the same field.
		for(i = 0; i < BATTLEFIELD_HEIGHT; ++i)
		{
			for(j = 0; j < BATTLEFIELD_WIDTH; ++j)
			{
				if (!battle.field[i][j]) continue;
				pawn = battle.field[i][j];

				battle_fight(players, players_count, pawn, count, damage);

				do
				{
					alliance = players[pawn->slot->player].alliance;

					d = 0;

					// Each alliance deals damage equally to each enemy unit.
					for(a = 0; a < players_count; ++a)
					{
						if (a == alliance) continue;

						// Calculate the number of enemy units for a.
						c = count[players_count - 1] - (count[a] - (a ? count[a - 1] : 0));

						d += (double)((damage[a] - (a ? damage[a - 1] : 0)) * pawn->slot->count) / c + 0.5;
					}

					// TODO implement the too many units sanction
					// d = ...;

					// Deal damage and kill some of the units.
					if (d) battle_damage(battle.field, pawn, d);

					// TODO handle dead pawns
				} while (pawn = pawn->_next);
			}
		}
#if !TEST
	} while ((status = battle_end(&battle, region->owner)) < 0);
#else
	} while (0);
#endif

	input_player(0, players); // TODO fix this

finally:

	free(count);
	free(damage);

	// TODO free each element in the heap
	heap_term(&collisions);

	return status;
}
