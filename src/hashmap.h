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
