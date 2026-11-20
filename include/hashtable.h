#ifndef RAIL_EASE_HASHTABLE_H
#define RAIL_EASE_HASHTABLE_H

#include "models.h"

typedef struct BookingRecord {
    Booking booking;
    struct BookingRecord *hash_next;
    struct BookingRecord *wl_prev;
    struct BookingRecord *wl_next;
    size_t heap_index;
} BookingRecord;

/* Find by PNR; params: context, PNR. Returns record/NULL. Average O(1), worst O(n). */
BookingRecord *booking_hash_find(const SystemContext *context, const char *pnr);

/* Insert unique record; params: context, owned record. Returns error. Amortized average O(1), worst O(n). */
RailError booking_hash_insert(SystemContext *context, BookingRecord *record);

/* Detach by PNR; params: context, PNR. Returns record/NULL. Average O(1), worst O(n). */
BookingRecord *booking_hash_remove(SystemContext *context, const char *pnr);

/* Free all records; params: context. Returns nothing. O(n) time, O(1) auxiliary space. */
void booking_hash_destroy(SystemContext *context);

#endif
