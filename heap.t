// The arguments with which the macro functions are called are guaranteed to produce no side effects.

// #define heap_type int
#if !defined(heap_type)
# error "Generic header argument heap_type not defined"
#endif

// Returns whether a is on top of b in the heap.
// #define heap_diff(a, b) ((a) >= (b))
#if !defined(heap_diff)
# error "Generic header argument heap_diff not defined"
#endif

// Called after an element changes its position in the heap (including initial push).
// #define heap_update(heap, position)
#if !defined(heap_update)
# error "Generic header argument heap_update not defined"
#endif

#include <stdlib.h>

#define HEAP_SIZE_BASE 16

// Returns the biggest element in the heap
#define heap_front(h) (*(h)->data)

struct heap_dynamic
{
	heap_type *data; // Array with the elements.
	size_t count; // Number of elements actually in the heap.
	size_t size; // Number of elements that the heap can hold withour resizing.
};

struct heap
{
	heap_type *data; // Array with the elements.
	size_t count; // Number of elements actually in the heap.
};

// Push element to the heap.
static void heap_push(struct heap *restrict heap, heap_type value)
{
	size_t index, parent;

	// Find out where to put the element and put it.
	for(index = heap->count++; index; index = parent)
	{
		parent = (index - 1) / 2;
		if (heap_diff(heap->data[parent], value)) break;
		heap->data[index] = heap->data[parent];
		heap_update(heap, index);
	}
	heap->data[index] = value;
	heap_update(heap, index);
}

// Removes the biggest element from the heap.
static void heap_pop(struct heap *heap)
{
	size_t index, swap, other;

	// Remove the biggest element.
	heap_type temp = heap->data[--heap->count];

	// Reorder the elements.
	index = 0;
	while (1)
	{
		// Find which child to swap with.
		swap = index * 2 + 1;
		if (swap >= heap->count) break; // If there are no children, the heap is reordered.
		other = swap + 1;
		if ((other < heap->count) && heap_diff(heap->data[other], heap->data[swap])) swap = other;
		if (heap_diff(temp, heap->data[swap])) break; // If the bigger child is less than or equal to its parent, the heap is reordered.

		heap->data[index] = heap->data[swap];
		heap_update(heap, index);
		index = swap;
	}
	heap->data[index] = temp;
	heap_update(heap, index);
}

// Move an element closer to the front of the heap.
static void heap_emerge(struct heap *restrict heap, size_t index)
{
	size_t parent;

	heap_type temp = heap->data[index];

	for(; index; index = parent)
	{
		parent = (index - 1) / 2;
		if (heap_diff(heap->data[parent], temp)) break;
		heap->data[index] = heap->data[parent];
		heap_update(heap, index);
	}
	heap->data[index] = temp;
	heap_update(heap, index);
}

// Heapifies a non-empty array.
static void heapify(struct heap *heap)
{
	unsigned item, index, swap, other;
	heap_type temp;

	if (heap->count < 2) return;

	// Move each non-leaf element to its correct position in its subtree.
	item = (heap->count / 2) - 1;
	while (1)
	{
		// Find the position of the current element in its subtree.
		temp = heap->data[item];
		index = item;
		while (1)
		{
			// Find the child to swap with.
			swap = index * 2 + 1;
			if (swap >= heap->count) break; // If there are no children, the current element is positioned.
			other = swap + 1;
			if ((other < heap->count) && heap_diff(heap->data[other], heap->data[swap])) swap = other;
			if (heap_diff(temp, heap->data[swap])) break; // If the bigger child is less than or equal to the parent, the heap is reordered.

			heap->data[index] = heap->data[swap];
			heap_update(heap, index);
			index = swap;
		}
		if (index != item) heap->data[index] = temp;

		if (!item) return;
		--item;
	}
}

// Removes the biggest element from the heap.
// Shrinks the allocated memory if necessary.
static void heap_dynamic_pop(struct heap_dynamic *heap)
{
	size_t index, swap, other;

	// Remove the biggest element.
	heap_type temp = heap->data[--heap->count];

	// Resize the heap if it's consuming too much memory.
	if ((heap->count <= (heap->size / 4)) && (heap->size > HEAP_SIZE_BASE))
	{
		heap->size /= 2;
		heap->data = realloc(heap->data, sizeof(heap_type) * heap->size);
	}

	// Reorder the elements.
	index = 0;
	while (1)
	{
		// Find which child to swap with.
		swap = index * 2 + 1;
		if (swap >= heap->count) break; // If there are no children, the heap is reordered.
		other = swap + 1;
		if ((other < heap->count) && heap_diff(heap->data[other], heap->data[swap])) swap = other;
		if (heap_diff(temp, heap->data[swap])) break; // If the bigger child is less than or equal to its parent, the heap is reordered.

		heap->data[index] = heap->data[swap];
		heap_update(heap, index);
		index = swap;
	}
	heap->data[index] = temp;
	heap_update(heap, index);
}

// Returns the biggest element in the heap
#define heap_dynamic_front(h) (*(h)->data)

// Frees the allocated memory
#define heap_dynamic_term(h) (free((h)->data))

static int heap_dynamic_init(struct heap_dynamic *heap)
{
	heap->data = malloc(HEAP_SIZE_BASE * sizeof(heap_type));
	if (!heap->data) return 0; // memory error
	heap->size = HEAP_SIZE_BASE;
	heap->count = 0;
	return 1;
}

// Inserts element to the heap.
// Expands the allocated memory if necessary.
static int heap_dynamic_push(struct heap_dynamic *restrict heap, heap_type value)
{
	size_t index, parent;

	// Resize the heap if it is too small to hold all the data.
	if (heap->count == heap->size)
	{
		heap_type *buffer;
		heap->size *= 2;
		buffer = realloc(heap->data, sizeof(heap_type) * heap->size);
		if (heap->data) heap->data = buffer;
		else return 0;
	}

	// Find out where to put the element and put it.
	for(index = heap->count++; index; index = parent)
	{
		parent = (index - 1) / 2;
		if (heap_diff(heap->data[parent], value)) break;
		heap->data[index] = heap->data[parent];
		heap_update(heap, index);
	}
	heap->data[index] = value;
	heap_update(heap, index);

	return 1;
}
