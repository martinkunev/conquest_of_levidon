// The arguments with which the macro functions are called are guaranteed to produce no side effects.

#if !defined(heap_name)
# define heap_name heap
#endif

#if !defined(heap_type)
# define heap_type void *
#endif

// Returns whether a is on top of b in the heap.
#if !defined(heap_above)
# define heap_above(a, b) ((a) >= (b))
#endif

// Called after an element changes its index in the heap (including initial push).
#if !defined(heap_update)
# define heap_update(heap, index)
#endif

#define NAME_CAT_EXPAND(a, b) a ## b
#define NAME_CAT(a, b) NAME_CAT_EXPAND(a, b)
#define NAME(suffix) NAME_CAT(heap_name, suffix)

#define STRING_EXPAND(string) #string
#define STRING(string) STRING_EXPAND(string)

#if defined(HEADER) || defined(SOURCE)
# define STATIC
#else
# define STATIC static
#endif

#if !defined(SOURCE)
struct heap_name
{
	heap_type *data; // Array with the elements.
	size_t count; // Number of elements actually in the heap.
};

STATIC void NAME(_push)(struct heap_name *heap, heap_type value);
STATIC void NAME(_pop)(struct heap_name *heap);
STATIC void NAME(_emerge)(struct heap_name *heap, size_t index);
STATIC void NAME(_heapify)(struct heap_name *heap);
#endif

#if !defined(HEADER)

#if defined(SOURCE)
# define INCLUDE0 #include <stdlib.h>
# define INCLUDE1 #include STRING(HEADER_NAME)
INCLUDE0
INCLUDE1
#else
# include <stdlib.h>
#endif

// Push element to the heap.
STATIC void NAME(_push)(struct heap_name *heap, heap_type value)
{
	size_t index, parent;

	// Find out where to put the element and put it.
	for(index = heap->count++; index; index = parent)
	{
		parent = (index - 1) / 2;
		if (heap_above(heap->data[parent], value)) break;
		heap->data[index] = heap->data[parent];
		heap_update(heap, index);
	}
	heap->data[index] = value;
	heap_update(heap, index);
}

// Removes the biggest element from the heap.
STATIC void NAME(_pop)(struct heap_name *heap)
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
		if ((other < heap->count) && heap_above(heap->data[other], heap->data[swap])) swap = other;
		if (heap_above(temp, heap->data[swap])) break; // If the bigger child is less than or equal to its parent, the heap is reordered.

		heap->data[index] = heap->data[swap];
		heap_update(heap, index);
		index = swap;
	}
	heap->data[index] = temp;
	heap_update(heap, index);
}

// Move an element closer to the front of the heap.
STATIC void NAME(_emerge)(struct heap_name *heap, size_t index) // TODO ? rename to heap_sift_up
{
	size_t parent;

	heap_type temp = heap->data[index];

	for(; index; index = parent)
	{
		parent = (index - 1) / 2;
		if (heap_above(heap->data[parent], temp)) break;
		heap->data[index] = heap->data[parent];
		heap_update(heap, index);
	}
	heap->data[index] = temp;
	heap_update(heap, index);
}

// Heapifies a non-empty array.
STATIC void NAME(_heapify)(struct heap_name *heap)
{
	unsigned item, index, swap, other;
	heap_type temp;

	if (heap->count < 2) return;

	// Move each non-leaf element down in its subtree until it satisfies the heap property.
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
			if (swap >= heap->count) break; // If there are no children, the element is placed properly.
			other = swap + 1;
			if ((other < heap->count) && heap_above(heap->data[other], heap->data[swap])) swap = other;
			if (heap_above(temp, heap->data[swap])) break; // If the bigger child is less than or equal to the parent, the element is placed properly.

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

#endif /* !defined(HEADER) */

#undef STATIC

#undef STRING
#undef STRING_EXPAND

#undef NAME
#undef NAME_CAT
#undef NAME_CAT_EXPAND

#undef heap_update
#undef heap_above
#undef heap_type
#undef heap_name
