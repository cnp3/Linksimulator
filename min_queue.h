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

#ifndef __MIN_QUEUE_H_
#define __MIN_QUEUE_H_

#include <stddef.h> /* size_t */

/* Minimal priority queue,
 * provides O(log n) on push and pop, O(1) on peek
 */

typedef struct minqueue minqueue_t;

/* Compare two keys
 * @return: non-zero if a > b, else 0
 */
typedef int (*minq_key_cmp)(const void *a, const void *b);

/* Create and initialize a new min-queue
 * @minq_key_cmp: The key compare function
 * @return: NULL on error
 */
minqueue_t *minq_new(minq_key_cmp);
/* Destroy a min-queue instance */
void minq_del(minqueue_t*);

/* Insert a new element in the min-queue
 * @minqueue_t: The queue
 * @val: the data to insert
 * @return: non-zero value on error (queue is then untouched)
 */
int minq_push(minqueue_t*, void *val);
/* Remove the minimal element of the queue */
void minq_pop(minqueue_t*);
/* Get the minimal element of the queue */
void* minq_peek(const minqueue_t*);
/* Check whether the queue is empty or not
 * @return: 0 is the queue is non-empty, non-zero otherwise
 */
int minq_empty(const minqueue_t*);
/* How many items in the queue? */
size_t minq_size(const minqueue_t*);

#endif
