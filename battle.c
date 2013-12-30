#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "heap.h"
#include "battle.h"
#include "interface.h"

/*
in-battle commands:
- get all pawns of a player
	for player number n, it's an array that starts at element n of ...
- get all pawns at a field (x, y)
	linked list starting at battlefield[y][x]
*/

/* plan:
- add fighting
- simplify code
*/

// Alliance number must be less than the number of players.

/*
damage depends on whether the other side is fighting (fleeing will lead to more damage received)
*/

// When escaping from enemy, unit speed is reduced by 1.

// Constraints necessary to ensure that no overflow can happen:
// x < 32
// y < 32
// t <= 8

#define DIAMETER 1

struct encounter
{
	double moment;
	unsigned pawns[2];
};

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

static void print(struct pawn *battlefield[BATTLEFIELD_HEIGHT][BATTLEFIELD_WIDTH]) // TODO change this
{
	unsigned char x, y;
	for(y = 0; y < BATTLEFIELD_HEIGHT; ++y)
	{
		//write(1, "+-+-+-+-+-+-+-+-+-+-+-+-+\n", BATTLEFIELD_WIDTH * 2 + 1 + 1);
		write(1, "+-+-+-+-+-+-+-+-++-+-+-+-+-+-+-+\n", BATTLEFIELD_WIDTH * 2 + 1 + 1);
		write(1, "|", 1);
		for(x = 0; x < BATTLEFIELD_WIDTH; ++x)
		{
			if (battlefield[y][x]) write(1, "*|", 2);
			else write(1, " |", 2);
		}
		write(1, "\n", 1);
	}
	//write(1, "+-+-+-+-+-+-+-+-+-+-+-+-+\n", BATTLEFIELD_WIDTH * 2 + 1 + 1);
	write(1, "+-+-+-+-+-+-+-+-++-+-+-+-+-+-+-+\n", BATTLEFIELD_WIDTH * 2 + 1 + 1);
}

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

static void pawn_remove(struct pawn *battlefield[BATTLEFIELD_HEIGHT][BATTLEFIELD_WIDTH], struct pawn *pawn)
{
	// Remove the pawn from its current field.
	if (pawn->_prev) pawn->_prev->_next = pawn->_next;
	if (pawn->_next) pawn->_next->_prev = pawn->_prev;

	// If there are no pawns left at the field, mark the field as empty.
	if (!pawn->_prev && !pawn->_next) battlefield[pawn->move.y[0]][pawn->move.x[0]] = 0;
}

static void pawn_move(struct pawn *battlefield[BATTLEFIELD_HEIGHT][BATTLEFIELD_WIDTH], struct pawn *pawn, unsigned char x, unsigned char y)
{
	// Remove the pawn from its current field.
	if (pawn->_prev) pawn->_prev->_next = pawn->_next;
	if (pawn->_next) pawn->_next->_prev = pawn->_prev;

	// If there are no pawns left at the field, mark the field as empty.
	if (!pawn->_prev && !pawn->_next) battlefield[pawn->move.y[0]][pawn->move.x[0]] = 0;

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

	struct pawn *pawn;

	// Move all unmoved pawns.
	size_t move;
	for(move = 0; move < pawns_count; ++move)
	{
		if ((pawns[move].move.x[1] == pawns[move].move.x[0]) && (pawns[move].move.y[1] == pawns[move].move.y[0]))
			continue;

		pawn_move(battlefield, pawns + move, pawns[move].move.x[1], pawns[move].move.y[1]);
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
		// TODO this is a stupid implementation. look for a better one

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

void battle_init(struct pawn *battlefield[BATTLEFIELD_HEIGHT][BATTLEFIELD_WIDTH], struct pawn *restrict pawns, size_t pawns_count)
{
	memset((void *)battlefield, 0, BATTLEFIELD_HEIGHT * BATTLEFIELD_WIDTH * sizeof(struct pawn *));

	// Put the pawns on the battlefield.
	unsigned char x, y;
	size_t i;
	for(i = 0; i < pawns_count; ++i)
	{
		// TODO set some pawn properties (hurt, _prev, _next, etc.)

		x = pawns[i].move.x[0];
		y = pawns[i].move.y[0];
		if (battlefield[y][x])
		{
			battlefield[y][x]->_prev = pawns + i;
			pawns[i]._next = battlefield[y][x];
		}
		battlefield[y][x] = pawns + i;
	}
}

int battle(const struct player *restrict players, size_t players_count, struct pawn *restrict pawns, size_t pawns_count)
{
	size_t i, j;

	unsigned *count, *damage;
	struct pawn *pawn;
	unsigned char alliance;

	int status;

	struct heap collisions;
	if (!heap_init(&collisions)) return -1;

	struct pawn *battlefield[BATTLEFIELD_HEIGHT][BATTLEFIELD_WIDTH];
	battle_init(battlefield, pawns, pawns_count);

	if_set(battlefield);

	input_player(0);

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

	unsigned char a;
	unsigned c, d;

	int dx, dy;

	// Deal damage to each pawn escaping from enemy pawns.
	for(i = 0; i < BATTLEFIELD_HEIGHT; ++i)
	{
		for(j = 0; j < BATTLEFIELD_WIDTH; ++j)
		{
			if (!battlefield[i][j]) continue;
			pawn = battlefield[i][j];

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
				battle_damage(battlefield, pawn, d);

				// TODO handle dead pawns
			} while (pawn = pawn->_next);
		}
	}

	status = battle_round(players, battlefield, pawns, pawns_count, &collisions);

	// Deal damage to enemy pawns located on the same field.
	for(i = 0; i < BATTLEFIELD_HEIGHT; ++i)
	{
		for(j = 0; j < BATTLEFIELD_WIDTH; ++j)
		{
			if (!battlefield[i][j]) continue;
			pawn = battlefield[i][j];

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
				battle_damage(battlefield, pawn, d);

				// TODO handle dead pawns
			} while (pawn = pawn->_next);
		}
	}

	input_player(0);

	// TODO win/lose conditions

finally:

	free(count);
	free(damage);

	// TODO free each element in the heap
	heap_term(&collisions);

	return status;
}
