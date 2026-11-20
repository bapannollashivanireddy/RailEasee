#include <stdlib.h>

#include "segtree.h"

/* Validate a half-open segment interval. O(1) time and O(1) space. */
static bool valid_range(const SeatBitmap *bitmap, unsigned from, unsigned to)
{
    return bitmap != NULL && from < to && to <= bitmap->segment_count &&
        bitmap->segment_count <= RAIL_MAX_JOURNEY_SEGMENTS;
}

/* Construct a contiguous interval mask. O(1) time and O(1) space. */
static uint16_t range_mask(unsigned from, unsigned to)
{
    uint32_t high = (UINT32_C(1) << to) - UINT32_C(1);
    uint32_t low = (UINT32_C(1) << from) - UINT32_C(1);
    return (uint16_t)(high ^ low);
}

/* Allocate occupancy masks. O(seats) time and O(seats) space. */
RailError seat_bitmap_init(SeatBitmap *bitmap, size_t seats, unsigned segments)
{
    if (bitmap == NULL || seats == 0 || segments == 0 ||
        segments > RAIL_MAX_JOURNEY_SEGMENTS) {
        return RAIL_ERR_INVALID;
    }
    bitmap->occupied_segments = calloc(seats, sizeof(*bitmap->occupied_segments));
    if (bitmap->occupied_segments == NULL) {
        return RAIL_ERR_MEMORY;
    }
    bitmap->seat_count = seats;
    bitmap->segment_count = segments;
    return RAIL_OK;
}

/* Find the first seat free over a journey range. O(seats) time, O(1) space. */
int seat_bitmap_find(const SeatBitmap *bitmap, unsigned from, unsigned to)
{
    size_t seat;
    uint16_t mask;
    if (!valid_range(bitmap, from, to)) {
        return -1;
    }
    mask = range_mask(from, to);
    for (seat = 0; seat < bitmap->seat_count; ++seat) {
        if ((bitmap->occupied_segments[seat] & mask) == 0) {
            return (int)seat;
        }
    }
    return -1;
}

/* Mark the requested segments occupied. O(1) time and O(1) space. */
RailError seat_bitmap_occupy(SeatBitmap *bitmap, size_t seat,
                             unsigned from, unsigned to)
{
    uint16_t mask;
    if (!valid_range(bitmap, from, to) || seat >= bitmap->seat_count) {
        return RAIL_ERR_INVALID;
    }
    mask = range_mask(from, to);
    if ((bitmap->occupied_segments[seat] & mask) != 0) {
        return RAIL_ERR_FULL;
    }
    bitmap->occupied_segments[seat] |= mask;
    return RAIL_OK;
}

/* Clear the requested segments. O(1) time and O(1) space. */
RailError seat_bitmap_release(SeatBitmap *bitmap, size_t seat,
                              unsigned from, unsigned to)
{
    if (!valid_range(bitmap, from, to) || seat >= bitmap->seat_count) {
        return RAIL_ERR_INVALID;
    }
    bitmap->occupied_segments[seat] &= (uint16_t)~range_mask(from, to);
    return RAIL_OK;
}

/* Check range availability. O(1) time and O(1) space. */
bool seat_bitmap_available(const SeatBitmap *bitmap, size_t seat,
                           unsigned from, unsigned to)
{
    if (!valid_range(bitmap, from, to) || seat >= bitmap->seat_count) {
        return false;
    }
    return (bitmap->occupied_segments[seat] & range_mask(from, to)) == 0;
}

/* Release bitmap memory. O(1) time and O(1) space. */
void seat_bitmap_destroy(SeatBitmap *bitmap)
{
    if (bitmap != NULL) {
        free(bitmap->occupied_segments);
        bitmap->occupied_segments = NULL;
        bitmap->seat_count = 0;
        bitmap->segment_count = 0;
    }
}
