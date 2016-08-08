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

#define PLAYERS_LIMIT 16
#define NEIGHBORS_LIMIT 8

#define TRAIN_QUEUE 4

#define NAME_LIMIT 32

#define UNIT_SPEED_LIMIT 12

enum weapon {WEAPON_NONE, WEAPON_CLUB, WEAPON_ARROW, WEAPON_CLEAVING, WEAPON_POLEARM, WEAPON_BLADE, WEAPON_BLUNT};
enum armor {ARMOR_NONE, ARMOR_LEATHER, ARMOR_CHAINMAIL, ARMOR_PLATE, ARMOR_WOODEN, ARMOR_STONE};

struct resources
{
	int gold;
	int food;
	int wood;
	int iron;
	int stone;
};

struct player
{
	//enum {Neutral, Local, Computer, Remote} type;
	enum {Neutral, Local, Computer} type;
	struct resources treasury;
	unsigned char alliance;
};

struct game
{
	struct player *players;
	size_t players_count;

	struct region *regions;
	size_t regions_count;

	unsigned turn; // TODO implement this
};

struct unit
{
	char name[NAME_LIMIT];
	size_t name_length;

	size_t index;

	uint32_t requires;
	unsigned troops_count;
	struct resources cost, income; // cost - one-time payment for ordering; income - daily resources change
	unsigned char time;

	unsigned char speed;
	unsigned char health;
	enum armor armor;

	struct
	{
		enum weapon weapon;
		double damage;
		double agility;
	} melee;
	struct
	{
		enum weapon weapon;
		double damage;
		unsigned char range;
	} ranged;
};

static inline int allies(const struct game *game, unsigned player0, unsigned player1)
{
	return (game->players[player0].alliance == game->players[player1].alliance);
}
