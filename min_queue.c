/*  vi:ts=4:sw=4:noet
The MIT License (MIT)

Copyright (c) 2015 Olivier Tilmans, olivier.tilmans@uclouvain.be

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
*/

#include "min_queue.h"

#include <stdlib.h> /* malloc */
#include <string.h> /* memcpy */

/* How many item slots per allocation steps */
#define SLOTS_PER_MALLOC 20

/* We use a heap to store all items,
 * this enables us to store it as a complete binary tree where each node
 * is a slot in a contiguous array. We can then navigate the tree levels
 * by multiplying or dividing by 2 the current level.
 * This can also be easily adapted to use k-ary trees.
 *
 * Invariant to be maintained:
 * A node in the tree is always < than its child nodes,
 * => The root is the minimal node
 */

/* Index of left child of x */
#define LCHILD(x) ((x) << 1)
/* Index of right child of x */
#define RCHILD(x) (((x) << 1) + 1)
/* Index of parent of x */
#define PARENT(x) ((x) >> 1)

/* The data structure we'll be using to represent the priority queue */
struct minqueue {
	minq_key_cmp cmp; /* Compare function for the elements */
	size_t size; /* The number of items in the queue */
	size_t alloc; /* The number of allocated slots */
	void **e; /* The array of slots in the queue */
};

minqueue_t *minq_new(minq_key_cmp cmp)
{
	minqueue_t *q;
	if (!cmp || !(q = malloc(sizeof(*q))))
		return NULL;
	/* Allocate multiple elements at once to reduce the calls to realloc */
	if (!(q->e = malloc(SLOTS_PER_MALLOC * sizeof(*q->e)))) {
		free(q);
		return NULL;
	}
	q->cmp = cmp;
	q->size = 0;
	q->alloc = SLOTS_PER_MALLOC;
	return q;
}

void minq_del(minqueue_t *q)
{
	if (!q) return;
	free(q->e);
	free(q);
}

int minq_push(minqueue_t* q, void *v)
{
	if (!q) return -1;
	/* Check if we have enough mem. slots */
	if (q->size == q->alloc) {
		/* We filled all slots, increase by an alloc step */
		size_t resize_to = q->size + SLOTS_PER_MALLOC;
		/* If we fail, we do not want to lose the previous array of elements */
		void **tmp;
		if (!(tmp = realloc(q->e, resize_to * sizeof(*q->e))))
			/* Failure, exit without changing the queue */
			return -1;
		/* Bookkeeping */
		q->e = tmp;
		q->alloc = resize_to;
	}
	/* Assume insertion at last index */
	size_t i = q->size++;
	size_t parent = PARENT(i);
	/* heapify-up: propagate the new value upwards as long as it is smaller
	 * than the parent of its insertion point, by swapping it with its parent
	 */
	while (i && q->cmp(q->e[parent], v)) {
		/* move parent down */
		q->e[i] = q->e[parent];
		/* Insertion point is one level above */
		i = parent;
		parent = PARENT(i);
	}
	/* Do the actual insertion */
	q->e[i] = v;
	return 0;
}

/* Return smallest child available, or the root (0) if none */
static inline size_t has_child(const minqueue_t *q, size_t i)
{
	size_t left = LCHILD(i);
	/* If we don't have child nodes, return the root of the tree */
	if (left >= q->size)
		return 0;
	size_t right = left+1;
	/* Check whether the right child is smaller than the left one */
	if (right < q->size && q->cmp(q->e[left], q->e[right]))
		/* right < left */
		return right;
	/* left < right */
	return left;
}

void minq_pop(minqueue_t *q)
{
	if (minq_empty(q)) return;
	/* Swap root with last entry and
	 * 'forget' about the last one, by decreasing the size*/
	q->e[0] = q->e[--q->size];
	/* We do not size down the alloc'd slots, the queue could grow again ... */
	/* heapify-down: Check that the current node is smaller than both of its
	 * child nodes, otherwise swap with the minimal child. */
	size_t current = 0, min_child;
	/* As long as current > min_child (if we have any) */
	while ((min_child = has_child(q, current)) &&
			q->cmp(q->e[current], q->e[min_child])) {
		/* Swap the two elements */
		void *tmp;
		tmp = q->e[current];
		q->e[current] = q->e[min_child];
		q->e[min_child] = tmp;
		/* Check if we need to push the swapped value further down */
		current = min_child;
	}
}

void* minq_peek(const minqueue_t *q)
{
	if (minq_empty(q)) return NULL;
	/* By definition, the minimal element of the queue is the root */
	return *q->e;
}

int minq_empty(const minqueue_t *q)
{
	return (!q || !q->size);
}

size_t minq_size(const minqueue_t *q)
{
	return q ? q->size : 0;
}
