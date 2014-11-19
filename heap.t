// #define heap_type int

// Returns whether a is on top of b in the heap.
// #define heap_diff(a, b) ((a) >= (b))

#include <stdlib.h>

#define BASE_SIZE 16

struct heap
{
	heap_type *data; // Array with the elements.
	size_t size; // Number of elements that the heap can hold withour resizing.
	size_t count; // Number of elements actually in the heap.
};

// Returns the biggest element in the heap
#define heap_front(h) (*(h)->data)

// Frees the allocated memory
#define heap_term(h) (free((h)->data))

static int heap_init(struct heap *h)
{
	h->data = malloc(BASE_SIZE * sizeof(heap_type));
	if (!h->data) return 0; // memory error
	h->size = BASE_SIZE;
	h->count = 0;
	return 1;
}

// Inserts element to the heap.
static int heap_push(struct heap *restrict h, heap_type value)
{
	size_t index, parent;

	// Resize the heap if it is too small to hold all the data.
	if (h->count == h->size)
	{
		heap_type *buffer;
		h->size *= 2;
		buffer = realloc(h->data, sizeof(heap_type) * h->size);
		if (h->data) h->data = buffer;
		else return 0;
	}

	// Find out where to put the element and put it.
	for(index = h->count++; index; index = parent)
	{
		parent = (index - 1) / 2;
		if (heap_diff(h->data[parent], value)) break;
		h->data[index] = h->data[parent];
	}
	h->data[index] = value;

	return 1;
}

// Removes the biggest element from the heap.
static int heap_pop(struct heap *h)
{
	size_t index, swap, other;

	// Remove the biggest element.
	heap_type temp = h->data[--h->count];

	// Resize the heap if it's consuming too much memory.
	if ((h->count <= (h->size / 4)) && (h->size > BASE_SIZE))
	{
		h->size /= 2;
		h->data = realloc(h->data, sizeof(heap_type) * h->size);
	}

	// Reorder the elements.
	index = 0;
	while (1)
	{
		// Find which child to swap with.
		swap = index * 2 + 1;
		if (swap >= h->count) break; // If there are no children, the heap is reordered.
		other = swap + 1;
		if ((other < h->count) && heap_diff(h->data[other], h->data[swap])) swap = other;
		if (heap_diff(temp, h->data[swap])) break; // If the bigger child is less than or equal to its parent, the heap is reordered.

		h->data[index] = h->data[swap];
		index = swap;
	}
	h->data[index] = temp;

	return 1;
}

// Heapifies a non-empty array.
static void heapify(heap_type *data, size_t count)
{
	unsigned item, index, swap, other;
	heap_type temp;

	// Move each non-leaf element to its correct position in its subtree.
	item = (count / 2) - 1;
	while (1)
	{
		// Find the position of the current element in its subtree.
		temp = data[item];
		index = item;
		while (1)
		{
			// Find the child to swap with.
			swap = index * 2 + 1;
			if (swap >= count) break; // If there are no children, the current element is positioned.
			other = swap + 1;
			if ((other < count) && heap_diff(data[other], data[swap])) swap = other;
			if (heap_diff(temp, data[swap])) break; // If the bigger child is less than or equal to the parent, the heap is reordered.

			data[index] = data[swap];
			index = swap;
		}
		if (index != item) data[index] = temp;

		if (!item) return;
		--item;
	}
}
