// WARNING: Requires C99 compatible compiler

typedef double *type;

struct heap
{
	size_t size; // Number of elements that the heap can hold withour resizing.
	size_t count; // Number of elements actually in the heap.
	type *data; // Array with the elements.
};

extern bool heap_init(struct heap *restrict h);
extern bool heap_push(struct heap *restrict h, type value);
extern void heap_pop(struct heap *restrict h);

// Returns the biggest element in the heap
#define heap_front(h) (*(h)->data)

// Frees the allocated memory
#define heap_term(h) (free((h)->data))

extern void heapify(type data[restrict], unsigned int count);
