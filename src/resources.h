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

int resource_enough(const struct resources *restrict total, const struct resources *restrict required);
void resource_add(struct resources *restrict total, const struct resources *restrict change);
void resource_subtract(struct resources *restrict total, const struct resources *restrict change);
void resource_spend(struct resources *restrict total, const struct resources *restrict spent);
void resource_multiply(struct resources *total, const struct resources *resource, unsigned factor);

int resources_adverse(const struct resources *restrict total, const struct resources *restrict change);

// TODO think how to prevent overflow
