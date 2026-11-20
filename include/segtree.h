#ifndef RAIL_EASE_SEGTREE_H
#define RAIL_EASE_SEGTREE_H

#include "models.h"

typedef struct {
    uint16_t *occupied_segments;
    size_t seat_count;
    unsigned segment_count;
} SeatBitmap;

/* Initialize bitmap; params: bitmap, seat count, segment count. Returns error. O(seats). */
RailError seat_bitmap_init(SeatBitmap *bitmap, size_t seats, unsigned segments);

/* Find available seat; params: bitmap, [from,to). Returns index/-1. O(seats) time. */
int seat_bitmap_find(const SeatBitmap *bitmap, unsigned from, unsigned to);

/* Occupy range; params: bitmap, seat, [from,to). Returns error. O(1) time. */
RailError seat_bitmap_occupy(SeatBitmap *bitmap, size_t seat,
                             unsigned from, unsigned to);

/* Release range; params: bitmap, seat, [from,to). Returns error. O(1) time. */
RailError seat_bitmap_release(SeatBitmap *bitmap, size_t seat,
                              unsigned from, unsigned to);

/* Query range; params: bitmap, seat, [from,to). Returns availability. O(1). */
bool seat_bitmap_available(const SeatBitmap *bitmap, size_t seat,
                           unsigned from, unsigned to);

/* Release bitmap; params: bitmap. Returns nothing. O(1) time and space. */
void seat_bitmap_destroy(SeatBitmap *bitmap);

#endif
