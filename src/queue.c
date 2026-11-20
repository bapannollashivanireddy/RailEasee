#include <stdlib.h>

#include "queue.h"

/* Allocate a bounded circular queue. O(1) time and O(capacity) space. */
RailError queue_init(RacQueue *queue, size_t capacity)
{
    if (queue == NULL || capacity == 0) {
        return RAIL_ERR_INVALID;
    }
    queue->items = calloc(capacity, sizeof(*queue->items));
    if (queue->items == NULL) {
        return RAIL_ERR_MEMORY;
    }
    queue->capacity = capacity;
    queue->head = 0;
    queue->size = 0;
    return RAIL_OK;
}

/* Append to the circular queue. O(1) time and O(1) space. */
RailError queue_push(RacQueue *queue, BookingRecord *record)
{
    size_t tail;
    if (queue == NULL || record == NULL) {
        return RAIL_ERR_INVALID;
    }
    if (queue->size == queue->capacity) {
        return RAIL_ERR_FULL;
    }
    tail = (queue->head + queue->size) % queue->capacity;
    queue->items[tail] = record;
    ++queue->size;
    return RAIL_OK;
}

/* Read the queue front. O(1) time and O(1) space. */
BookingRecord *queue_peek(const RacQueue *queue)
{
    return queue == NULL || queue->size == 0 ? NULL : queue->items[queue->head];
}

/* Remove the queue front. O(1) time and O(1) space. */
BookingRecord *queue_pop(RacQueue *queue)
{
    BookingRecord *record;
    if (queue == NULL || queue->size == 0) {
        return NULL;
    }
    record = queue->items[queue->head];
    queue->items[queue->head] = NULL;
    queue->head = (queue->head + 1) % queue->capacity;
    --queue->size;
    return record;
}

/* Remove a record while preserving FIFO order. O(n) time, O(1) space. */
bool queue_remove(RacQueue *queue, BookingRecord *record)
{
    size_t i;
    if (queue == NULL || record == NULL) {
        return false;
    }
    for (i = 0; i < queue->size; ++i) {
        size_t index = (queue->head + i) % queue->capacity;
        if (queue->items[index] == record) {
            size_t j;
            for (j = i; j + 1 < queue->size; ++j) {
                size_t current = (queue->head + j) % queue->capacity;
                size_t next = (queue->head + j + 1) % queue->capacity;
                queue->items[current] = queue->items[next];
            }
            queue->items[(queue->head + queue->size - 1) % queue->capacity] = NULL;
            --queue->size;
            return true;
        }
    }
    return false;
}

/* Release queue storage. O(1) time and O(1) space. */
void queue_destroy(RacQueue *queue)
{
    if (queue != NULL) {
        free(queue->items);
        queue->items = NULL;
        queue->capacity = 0;
        queue->head = 0;
        queue->size = 0;
    }
}
