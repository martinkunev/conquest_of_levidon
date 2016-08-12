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

enum {ANNEALING_STEPS = 64, ANNEALING_TRIES = 8};

#define ANNEALING_COOLDOWN 0.95

extern const double desire_buildings[];

double unit_importance(const struct unit *restrict unit, const struct garrison_info *restrict garrison);
double unit_cost_significance(const struct resources *restrict cost);
double unit_usefulness(const struct unit *restrict unit, unsigned troops_count);

int state_wanted(double rate, double rate_new, double temperature);
