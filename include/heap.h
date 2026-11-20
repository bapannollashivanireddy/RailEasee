#ifndef RAIL_EASE_HEAP_H
#define RAIL_EASE_HEAP_H

#include "hashtable.h"

typedef struct {
    BookingRecord **items;
    size_t size;
    size_t capacity;
} BookingHeap;

/* Initialize heap; params: heap. Returns nothing. O(1) time and space. */
void heap_init(BookingHeap *heap);

/* Insert record; params: heap, record. Returns error. O(log n) amortized. */
RailError heap_push(BookingHeap *heap, BookingRecord *record);

/* Read heap root; params: heap. Returns record/NULL. O(1) time and space. */
BookingRecord *heap_peek(const BookingHeap *heap);

/* Decrease key; params: heap, resident record, new quota/time. Returns error. O(log n). */
RailError heap_decrease_key(BookingHeap *heap, BookingRecord *record,
                            QuotaType quota, uint64_t timestamp);

/* Remove root; params: heap. Returns record/NULL. O(log n) time, O(1) space. */
BookingRecord *heap_pop(BookingHeap *heap);

/* Remove resident record; params: heap, record. Returns record/NULL. O(log n). */
BookingRecord *heap_remove(BookingHeap *heap, BookingRecord *record);

/* Release array; params: heap. Returns nothing. O(1) time and space. */
void heap_destroy(BookingHeap *heap);

#endif
