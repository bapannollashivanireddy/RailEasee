#ifndef RAIL_EASE_DLL_H
#define RAIL_EASE_DLL_H

#include "hashtable.h"

/* Ordered insert; params: list-head, record. Returns nothing. O(n) time, O(1) space. */
void wl_insert_ordered(BookingRecord **head, BookingRecord *record);

/* Detach node; params: list-head, record. Returns nothing. O(1) time and space. */
void wl_remove(BookingRecord **head, BookingRecord *record);

/* Find one-based position; params: list-head, record. Returns index/0. O(n) time. */
size_t wl_position(const BookingRecord *head, const BookingRecord *record);

#endif
