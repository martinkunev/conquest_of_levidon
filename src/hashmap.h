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

#if !defined(hashmap_type)
# define hashmap_type void *
#endif

struct hashmap
{
	size_t count;
	size_t slots_count;
	struct hashmap_entry
	{
		struct hashmap_entry *const next;
		const size_t key_size;
		hashmap_type value;
		const uint32_t hash;
		const unsigned char key_data[];
	} **slots;
};

struct hashmap_iterator
{
	size_t index;
	struct hashmap_entry **entry;
};

int hashmap_init(struct hashmap *hashmap, size_t slots_count);

hashmap_type *hashmap_get(const struct hashmap *restrict hashmap, const unsigned char *restrict key_data, size_t key_size);

hashmap_type *hashmap_insert(struct hashmap *restrict hashmap, const unsigned char *restrict key_data, size_t key_size, hashmap_type value);
hashmap_type *hashmap_insert_fast(struct hashmap *restrict hashmap, const unsigned char *restrict key_data, size_t key_size, hashmap_type value);
int hashmap_remove(struct hashmap *restrict hashmap, const unsigned char *restrict key_data, size_t key_size, hashmap_type *value_old);

struct hashmap_entry *hashmap_first(const struct hashmap *restrict hashmap, struct hashmap_iterator *restrict iterator);
struct hashmap_entry *hashmap_next(const struct hashmap *restrict hashmap, struct hashmap_iterator *restrict iterator);
void hashmap_remove_entry(struct hashmap *restrict hashmap, struct hashmap_iterator *restrict iterator);

void hashmap_term(struct hashmap *hashmap);

enum {HASHMAP_SIZE_DEFAULT = 32};

//int hashmap_set(struct hashmap *restrict hashmap, const struct bytes *restrict key, hashmap_type value, hashmap_type *value_old);
