// #define queue_type int

#include <stdlib.h>

struct queue
{
	struct queue_item
	{
		queue_type data;
		struct queue_item *next;
	} *first, *last;
	size_t length;
};

static void queue_init(struct queue *queue)
{
	queue->first = 0;
	queue->last = 0;
	queue->length = 0;
}

static int queue_push(struct queue *queue, queue_type data)
{
	struct queue_item *item = malloc(sizeof(struct queue_item));
	if (!item) return -1;

	item->data = data;
	item->next = 0;

	if (queue->last) queue->last->next = item;
	else queue->first = item;
	queue->last = item;
	queue->length += 1;

	return 0;
}

static queue_type queue_shift(struct queue *queue)
{
	struct queue_item *item = queue->first;
	if (!item) /*return 0*/; // TODO what to return on error

	queue->first = item->next;
	if (!queue->first) queue->last = 0; // update last if the queue is empty
	queue_type data = item->data;
	free(item);
	queue->length -= 1;

	return data;
}

static int queue_insert(struct queue *queue, struct queue_item *location, queue_type data)
{
	struct queue_item *item = malloc(sizeof(*item));
	if (!item) return -1;

	item->data = data;
	item->next = location->next;
	if (!item->next) queue->last = item->next; // update last if the inserted item is last
	location->next = item;

	return 0;
}

/*static queue_type queue_remove(struct queue *restrict queue, struct queue_item **item)
{
	// TODO this won't work with the current implementation of last
	struct queue_item *temp = *item;
	*item = temp->next;
	if (!temp->next) queue->last = item; // set last if this was the last item
	queue_type data = temp->data;
	free(temp);
	queue->length -= 1;
	return data;
}*/

static void queue_term(struct queue *queue)
{
	struct queue_item *item = queue->first, *next;
	while (item)
	{
		next = item->next;
		free(item);
		item = next;
	}
}

static void queue_term_free(struct queue *queue, void (*callback)(void *))
{
	struct queue_item *item = queue->first, *next;
	while (item)
	{
		next = item->next;
		callback(&item->data);
		free(item);
		item = next;
	}
}
