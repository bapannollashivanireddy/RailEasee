#include "dll.h"

/* Compare waiting-list priority keys. O(1) time and O(1) space. */
static bool record_before(const BookingRecord *a, const BookingRecord *b)
{
    if (a->booking.quota != b->booking.quota) {
        return a->booking.quota < b->booking.quota;
    }
    return a->booking.timestamp < b->booking.timestamp;
}

/* Insert in stable priority order. O(n) time and O(1) space. */
void wl_insert_ordered(BookingRecord **head, BookingRecord *record)
{
    BookingRecord *current;
    if (head == NULL || record == NULL) {
        return;
    }
    record->wl_prev = NULL;
    record->wl_next = NULL;
    if (*head == NULL || record_before(record, *head)) {
        record->wl_next = *head;
        if (*head != NULL) {
            (*head)->wl_prev = record;
        }
        *head = record;
        return;
    }
    current = *head;
    while (current->wl_next != NULL && !record_before(record, current->wl_next)) {
        current = current->wl_next;
    }
    record->wl_next = current->wl_next;
    record->wl_prev = current;
    if (current->wl_next != NULL) {
        current->wl_next->wl_prev = record;
    }
    current->wl_next = record;
}

/* Detach a node from a waiting-list view. O(1) time and O(1) space. */
void wl_remove(BookingRecord **head, BookingRecord *record)
{
    if (head == NULL || record == NULL) {
        return;
    }
    if (record->wl_prev != NULL) {
        record->wl_prev->wl_next = record->wl_next;
    } else if (*head == record) {
        *head = record->wl_next;
    } else {
        return;
    }
    if (record->wl_next != NULL) {
        record->wl_next->wl_prev = record->wl_prev;
    }
    record->wl_prev = NULL;
    record->wl_next = NULL;
}

/* Find a node's live position. O(n) time and O(1) space. */
size_t wl_position(const BookingRecord *head, const BookingRecord *record)
{
    size_t position = 1;
    while (head != NULL) {
        if (head == record) {
            return position;
        }
        head = head->wl_next;
        ++position;
    }
    return 0;
}
