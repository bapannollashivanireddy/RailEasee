#ifndef RAIL_EASE_QUEUE_H
#define RAIL_EASE_QUEUE_H

#include "hashtable.h"

typedef struct {
    BookingRecord **items;
    size_t capacity;
    size_t head;
    size_t size;
} RacQueue;

/* Initialize capacity; params: queue, capacity. Returns error. O(1) time, O(capacity) space. */
RailError queue_init(RacQueue *queue, size_t capacity);

/* Append record; params: queue, record. Returns error. O(1) time and space. */
RailError queue_push(RacQueue *queue, BookingRecord *record);

/* Read front; params: queue. Returns record/NULL. O(1) time and space. */
BookingRecord *queue_peek(const RacQueue *queue);

/* Remove front; params: queue. Returns record/NULL. O(1) time and space. */
BookingRecord *queue_pop(RacQueue *queue);

/* Remove known record; params: queue, record. Returns found. O(n) time, O(1) space. */
bool queue_remove(RacQueue *queue, BookingRecord *record);

/* Release array; params: queue. Returns nothing. O(1) time and space. */
void queue_destroy(RacQueue *queue);

#endif
