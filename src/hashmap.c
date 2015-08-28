// TODO how to make a header
// TODO hash function and key comparison can be made faster for custom cases
// TODO double indirection in the iterator is probably bad
// TODO use #if HASHMAP_HASHSET(hashmap_type)
// TODO try to reduce the number of casts (currently there are 4)

// Define the macro HASHMAP_HASHSET that will be used to check if hashmap_type is void.
// TODO this won't work for double pointers
// TODO this won't work for structs
/*#define HASHMAP_void 1
#define HASHMAP_EXPAND(type) HASHMAP_##type
#define HASHMAP_HASHSET(type) HASHMAP_EXPAND(type) + 0*/

#include <stddef.h>
#include <stdint.h>

struct hashmap_entry_mutable
{
	struct hashmap_entry *next;
	size_t key_size;
	hashmap_type value;
	uint32_t hash;
	unsigned char key_data[];
};

static inline int hashmap_key_eq(const struct hashmap_entry *restrict slot, const unsigned char *restrict key_data, size_t key_size, uint32_t hash)
{
	return ((key_size == slot->key_size) && (slot->hash == hash) && !memcmp(key_data, slot->key_data, slot->key_size));
	//return ((key_size == slot->key_size) && !memcmp(key_data, slot->key_data, slot->key_size));
}

// TODO: check this sdbm hash algorithm
static uint32_t hashmap_hash(const unsigned char data[], size_t size)
{
	uint32_t result = 0;
	size_t i;
	for(i = 0; i < size; ++i)
		result = data[i] + (result << 6) + (result << 16) - result;
	return result;
}

static inline size_t hashmap_index(const struct hashmap *hashmap, uint32_t hash)
{
	return hash & (hashmap->slots_count - 1);
}

int hashmap_init(struct hashmap *hashmap, size_t slots_count)
{
	size_t i;

	hashmap->count = 0;
	hashmap->slots_count = slots_count;
	hashmap->slots = malloc(slots_count * sizeof(*hashmap->slots));
	if (!hashmap->slots) return ERROR_MEMORY;

	// Set each slot separately to ensure entries is initialized properly.
	for(i = 0; i < slots_count; ++i)
		hashmap->slots[i] = 0;

	return 0;
}

hashmap_type *hashmap_get(const struct hashmap *restrict hashmap, const unsigned char *restrict key_data, size_t key_size)
{
	// Look for the requested key among the entries with the corresponding hash.

	uint32_t hash = hashmap_hash(key_data, key_size);
	struct hashmap_entry *entry;
	size_t index = hashmap_index(hashmap, hash);
	for(entry = hashmap->slots[index]; entry; entry = entry->next)
		if (hashmap_key_eq(entry, key_data, key_size, hash))
			return &entry->value; // found

	return 0; // missing
}

static int hashmap_expand(struct hashmap *hashmap)
{
	size_t i;

	size_t slots_count;
	struct hashmap_entry **slots;

	uint32_t mask = hashmap->slots_count;

	slots_count = hashmap->slots_count * 2;
	slots = malloc(slots_count * sizeof(*slots));
	if (!slots) return ERROR_MEMORY;

	for(i = 0; i < hashmap->slots_count; ++i)
	{
		struct hashmap_entry *entry, *entry_next;

		struct hashmap_entry **slot_even = (slots + i);
		struct hashmap_entry **slot_odd = (slots + (mask | i));

		*slot_even = 0;
		*slot_odd = 0;

		for(entry = hashmap->slots[i]; entry; entry = entry_next)
		{
			struct hashmap_entry_mutable *entry_mutable = (struct hashmap_entry_mutable *)entry;
			entry_next = entry->next;

			if (entry->hash & mask)
			{
				entry_mutable->next = *slot_odd;
				*slot_odd = entry;
			}
			else
			{
				entry_mutable->next = *slot_even;
				*slot_even = entry;
			}
		}
	}

	free(hashmap->slots);

	hashmap->slots = slots;
	hashmap->slots_count = slots_count;
	return 0;
}

static hashmap_type *insert(struct hashmap_entry **restrict slot, const unsigned char *restrict key_data, size_t key_size, uint32_t hash, hashmap_type value)
{
	struct hashmap_entry_mutable *entry_mutable;
	struct hashmap_entry *entry = malloc(offsetof(struct hashmap_entry, key_data) + key_size);
	if (!entry) return 0;
	entry_mutable = (struct hashmap_entry_mutable *)entry;

	// Set entry key and value.
	entry_mutable->key_size = key_size;
	entry_mutable->hash = hash;
	entry_mutable->value = value;
	memcpy(entry_mutable->key_data, key_data, key_size);

	// Insert the entry into the slot.
	entry_mutable->next = *slot;
	*slot = entry;

	return &entry->value;
}

hashmap_type *hashmap_insert(struct hashmap *restrict hashmap, const unsigned char *restrict key_data, size_t key_size, hashmap_type value)
{
	uint32_t hash = hashmap_hash(key_data, key_size);
	struct hashmap_entry *entry;

	// Look for an entry with the specified key in the hashmap.
	size_t index = hashmap_index(hashmap, hash);
	for(entry = hashmap->slots[index]; entry; entry = entry->next)
		if (hashmap_key_eq(entry, key_data, key_size, hash))
			return &entry->value; // the specified key exists in the hashmap

	// Enlarge the hashmap if the chances for collision are too high.
	if (hashmap->count >= hashmap->slots_count)
	{
		if (hashmap_expand(hashmap) < 0)
			return 0;

		// Update target insert slot.
		index = hashmap_index(hashmap, hash);
	}

	hashmap_type *result = insert(hashmap->slots + index, key_data, key_size, hash, value);
	if (result) hashmap->count += 1;
	return result;
}

hashmap_type *hashmap_insert_fast(struct hashmap *restrict hashmap, const unsigned char *restrict key_data, size_t key_size, hashmap_type value)
{
	uint32_t hash = hashmap_hash(key_data, key_size);
	size_t index;

	// Enlarge the hashmap if the chances for collision are too high.
	if (hashmap->count >= hashmap->slots_count)
		if (hashmap_expand(hashmap) < 0)
			return 0;

	// Find target insert slot.
	index = hashmap_index(hashmap, hash);

	hashmap_type *result = insert(hashmap->slots + index, key_data, key_size, hash, value);
	if (result) hashmap->count += 1;
	return result;
}

int hashmap_remove(struct hashmap *restrict hashmap, const unsigned char *restrict key_data, size_t key_size, hashmap_type *value_old)
{
	// Look for the requested key among the items with the corresponding hash.

	uint32_t hash = hashmap_hash(key_data, key_size);
	struct hashmap_entry **slot, *entry;

	// TODO decrease number of slots?

	// Look for an entry with the specified key in the hashmap.
	for(slot = hashmap->slots + hashmap_index(hashmap, hash); entry = *slot; slot = (struct hashmap_entry **)&entry->next)
		if (hashmap_key_eq(entry, key_data, key_size, hash))
		{
			if (value_old) *value_old = entry->value;

			free(entry);
			*slot = 0;
			hashmap->count -= 1;

			return 0;
		}

	return ERROR_MISSING;
}

struct hashmap_entry *hashmap_first(const struct hashmap *restrict hashmap, struct hashmap_iterator *restrict iterator)
{
	size_t index = 0;
	do
	{
		if (hashmap->slots[index])
		{
			iterator->index = index;
			iterator->entry = hashmap->slots + index;
			return *iterator->entry;
		}
		index += 1;
	} while (index < hashmap->slots_count);

	return 0; // empty
}

static int hashmap_iterate(const struct hashmap *restrict hashmap, struct hashmap_iterator *restrict iterator)
{
	if ((*iterator->entry)->next)
	{
		iterator->entry = (struct hashmap_entry **)&(*iterator->entry)->next;
		return 0;
	}

	size_t index = iterator->index;
	do
	{
		index += 1;
		if (index == hashmap->slots_count) return ERROR_MISSING; // no more entries
	} while (!hashmap->slots[index]);

	iterator->index = index;
	iterator->entry = hashmap->slots + index;
	return 0;
}

struct hashmap_entry *hashmap_next(const struct hashmap *restrict hashmap, struct hashmap_iterator *restrict iterator)
{
	if (hashmap_iterate(hashmap, iterator) < 0) return 0; // no more entries
	return *iterator->entry;
}

void hashmap_remove_entry(struct hashmap *restrict hashmap, struct hashmap_iterator *restrict iterator)
{
	struct hashmap_entry *entry = *iterator->entry;
	hashmap_iterate(hashmap, iterator);
	free(entry);
	hashmap->count -= 1;
}

void hashmap_term(struct hashmap *hashmap)
{
	size_t i;
	for(i = 0; i < hashmap->slots_count; ++i)
	{
		struct hashmap_entry *entry, *entry_next;
		for(entry = hashmap->slots[i]; entry; entry = entry_next)
		{
			entry_next = entry->next;
			free(entry);
		}
	}
	free(hashmap->slots);
}

// TODO remove this?
/*int hashmap_set(struct hashmap *restrict hashmap, const struct bytes *restrict key, hashmap_type value, hashmap_type *value_old)
{
	uint32_t hash = hashmap_hash(key->data, key->size);
	struct slot *slot = hashmap_slot(hashmap->slot, hashmap->count_slots, hash);

	// Look for an item with the specified key in the hashmap.
	unsigned char *entries = slot->entries;
	if (entries)
	{
		struct hashmap_entry_mutable *entry;
		size_t offset;
		for(offset = 0; offset < slot->size; offset += entry_size(entry->key_size))
		{
			entry = (struct hashmap_entry_mutable *)(entries + offset);

			if (hashmap_key_eq(entry, key))
			{
				// Replace the old value with the new one.
				// Store the old value in the memory pointed to by value_old.
				if (value_old) *value_old = entry->value;
				entry->value = value;
				return 0;
			}
		}
	}

	// Enlarge the hashmap if the chances for collision are too high.
	if (hashmap->count >= hashmap->count_slots)
	{
		if (hashmap_expand(hashmap) < 0)
			return ERROR_MEMORY;

		// Update target slot pointer.
		slot = hashmap_slot(hashmap->slot, hashmap->count_slots, hash);
	}

	hashmap_type *result = insert(slot, key->data, key->size, hash, value);
	if (result)
	{
		hashmap->count += 1;
		return 0;
	}
	else return ERROR_MEMORY;
}*/
