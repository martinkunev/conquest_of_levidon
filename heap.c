// WARNING: Requires C99 compatible compiler

#include <stdbool.h>
#include <stdlib.h>

#include "heap.h"

// true		a is in front of b
// false	b is in front of a
#define CMP(a, b) ((a) <= (b))

#define BASE_SIZE 16

bool heap_init(struct heap *restrict h)
{
	h->data = malloc(sizeof(type) * BASE_SIZE);
	if (!h->data) return false; // memory error
	h->size = BASE_SIZE;
	h->count = 0;
	return true;
}

// Inserts element to the heap.
bool heap_push(struct heap *restrict h, type value)
{
	size_t index, parent;

	// Resize the heap if it is too small to hold all the data.
	if (h->count == h->size)
	{
		type *buffer;
		h->size <<= 1;
		buffer = realloc(h->data, sizeof(type) * h->size);
		if (h->data) h->data = buffer;
		else return false;
	}

	// Find out where to put the element and put it.
	for(index = h->count++; index; index = parent)
	{
		parent = (index - 1) >> 1;
		if CMP(h->data[parent], value) break;
		h->data[index] = h->data[parent];
	}
	h->data[index] = value;

	return true;
}

// Removes the biggest element from the heap.
void heap_pop(struct heap *restrict h)
{
	size_t index, swap, other;

	// Remove the biggest element.
	type temp = h->data[--h->count];

	// Resize the heap if it's consuming too much memory.
	if ((h->count <= (h->size >> 2)) && (h->size > BASE_SIZE))
	{
		h->size >>= 1;
		h->data = realloc(h->data, sizeof(type) * h->size);
	}

	// Reorder the elements.
	for(index = 0; 1; index = swap)
	{
		// Find which child to swap with.
		swap = (index << 1) + 1;
		if (swap >= h->count) break; // If there are no children, the heap is reordered.
		other = swap + 1;
		if ((other < h->count) && CMP(h->data[other], h->data[swap])) swap = other;
		if CMP(temp, h->data[swap]) break; // If the bigger child is less than or equal to its parent, the heap is reordered.

		h->data[index] = h->data[swap];
	}
	h->data[index] = temp;
}

// Heapifies a non-empty array
void heapify(type data[restrict], unsigned int count)
{
	unsigned int item, index, swap, other;
	type temp;

	// Move every non-leaf element to the right position in its subtree
	item = (count >> 1) - 1;
	while (1)
	{
		// Find the position of the current element in its subtree
		temp = data[item];
		for(index = item; 1; index = swap)
		{
			// Find the child to swap with
			swap = (index << 1) + 1;
			if (swap >= count) break; // If there are no children, the current element is positioned
			other = swap + 1;
			if ((other < count) && CMP(data[other], data[swap])) swap = other;
			if CMP(temp, data[swap]) break; // If the bigger child is smaller than or equal to the parent, the heap is reordered

			data[index] = data[swap];
		}
		if (index != item) data[index] = temp;

		if (!item) return;
		--item;
	}
}

/*type heap_get(struct heap *restrict h)
{
	register type result = heap_front(h);
	heap_pop(h);
	return result;
}*/

/*struct heap *heap_create(void)
{
	struct heap *restrict h;
	h = malloc(sizeof(struct heap));
	if (!h) _exit(1); // Exit if the memory allocation fails
	heap_init(h);
	return h;
}*/

// Sorts array using the heap sort algorithm
/*void heap_sort(type values[restrict], int count)
{
	struct heap h;
	int i;
	register type temp;

	// TODO: this won't work with static memory because heap_pop deallocates memory

	// Build a heap
	h.size = count;
	h.count = count;
	h.data = values;
	heapify(&h);

	// Sort the elements by removing the biggest and putting it at the freed position
	for(i = count - 1; i >= 0; i--)
	{
		temp = heap_front(&h);
		heap_pop(&h);
		values[i] = temp;
	}
}*/
