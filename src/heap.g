// The arguments with which the macro functions are called are guaranteed to produce no side effects.

// #define heap_type int
#if !defined(heap_type)
# error "Generic header argument heap_type not defined"
#endif

// Returns whether a is on top of b in the heap.
#if !defined(heap_diff)
# define heap_diff(a, b) ((a) >= (b))
#endif

// Called after an element changes its position in the heap (including initial push).
// #define heap_update(heap, position)
#if !defined(heap_update)
# define heap_update(heap, position)
#endif

#include <stdlib.h>

#define HEAP_SIZE_BASE 16

// Returns the biggest element in the heap
#define heap_front(h) (*(h)->data)

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
		if (index != item)
		{
			heap->data[index] = temp;
			heap_update(heap, index);
		}

		if (!item) return;
		--item;
	}
}

#undef heap_update
#undef heap_diff
#undef heap_type
