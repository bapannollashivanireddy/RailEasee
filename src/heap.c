#include <stdlib.h>

#include "heap.h"

/* Compare heap priority keys. O(1) time and O(1) space. */
static bool before(const BookingRecord *a, const BookingRecord *b)
{
    if (a->booking.quota != b->booking.quota) {
        return a->booking.quota < b->booking.quota;
    }
    return a->booking.timestamp < b->booking.timestamp;
}

/* Swap heap entries and repair index-map values. O(1) time and space. */
static void exchange(BookingHeap *heap, size_t a, size_t b)
{
    BookingRecord *temporary = heap->items[a];
    heap->items[a] = heap->items[b];
    heap->items[b] = temporary;
    heap->items[a]->heap_index = a;
    heap->items[b]->heap_index = b;
}

/* Restore parent ordering after insertion. O(log n) time and O(1) space. */
static void sift_up(BookingHeap *heap, size_t index)
{
    while (index > 0) {
        size_t parent = (index - 1) / 2;
        if (!before(heap->items[index], heap->items[parent])) {
            break;
        }
        exchange(heap, index, parent);
        index = parent;
    }
}

/* Restore parent ordering after removal. O(log n) time and O(1) space. */
static void sift_down(BookingHeap *heap, size_t index)
{
    while (index * 2 + 1 < heap->size) {
        size_t child = index * 2 + 1;
        if (child + 1 < heap->size &&
            before(heap->items[child + 1], heap->items[child])) {
            ++child;
        }
        if (!before(heap->items[child], heap->items[index])) {
            break;
        }
        exchange(heap, index, child);
        index = child;
    }
}

/* Initialize an empty heap. O(1) time and O(1) space. */
void heap_init(BookingHeap *heap)
{
    if (heap != NULL) {
        heap->items = NULL;
        heap->size = 0;
        heap->capacity = 0;
    }
}

/* Insert an entry. O(log n) time, O(1) auxiliary space, amortized growth. */
RailError heap_push(BookingHeap *heap, BookingRecord *record)
{
    BookingRecord **grown;
    size_t capacity;
    if (heap == NULL || record == NULL) {
        return RAIL_ERR_INVALID;
    }
    if (heap->size == heap->capacity) {
        capacity = heap->capacity == 0 ? 8 : heap->capacity * 2;
        grown = realloc(heap->items, capacity * sizeof(*grown));
        if (grown == NULL) {
            return RAIL_ERR_MEMORY;
        }
        heap->items = grown;
        heap->capacity = capacity;
    }
    record->heap_index = heap->size;
    heap->items[heap->size++] = record;
    sift_up(heap, record->heap_index);
    return RAIL_OK;
}

/* Read the root. O(1) time and O(1) space. */
BookingRecord *heap_peek(const BookingHeap *heap)
{
    return heap == NULL || heap->size == 0 ? NULL : heap->items[0];
}

/* Decrease a resident key and restore heap order. O(log n) time, O(1) space. */
RailError heap_decrease_key(BookingHeap *heap, BookingRecord *record,
                            QuotaType quota, uint64_t timestamp)
{
    QuotaType old_quota;
    uint64_t old_timestamp;
    if (heap == NULL || record == NULL || quota < RAIL_QUOTA_GENERAL ||
        quota > RAIL_QUOTA_SENIOR ||
        record->heap_index >= heap->size ||
        heap->items[record->heap_index] != record) {
        return RAIL_ERR_INVALID;
    }
    old_quota = record->booking.quota;
    old_timestamp = record->booking.timestamp;
    if (quota > old_quota ||
        (quota == old_quota && timestamp > old_timestamp)) {
        return RAIL_ERR_INVALID;
    }
    record->booking.quota = quota;
    record->booking.timestamp = timestamp;
    sift_up(heap, record->heap_index);
    return RAIL_OK;
}

/* Remove the root entry. O(log n) time and O(1) auxiliary space. */
BookingRecord *heap_pop(BookingHeap *heap)
{
    return heap == NULL || heap->size == 0 ? NULL :
        heap_remove(heap, heap->items[0]);
}

/* Remove an arbitrary entry using its index map. O(log n), O(1) space. */
BookingRecord *heap_remove(BookingHeap *heap, BookingRecord *record)
{
    size_t index;
    BookingRecord *removed;
    if (heap == NULL || record == NULL || record->heap_index >= heap->size ||
        heap->items[record->heap_index] != record) {
        return NULL;
    }
    index = record->heap_index;
    removed = heap->items[index];
    --heap->size;
    if (index != heap->size) {
        heap->items[index] = heap->items[heap->size];
        heap->items[index]->heap_index = index;
        if (index > 0 && before(heap->items[index], heap->items[(index - 1) / 2])) {
            sift_up(heap, index);
        } else {
            sift_down(heap, index);
        }
    }
    removed->heap_index = SIZE_MAX;
    return removed;
}

/* Release heap storage. O(1) time and O(1) space. */
void heap_destroy(BookingHeap *heap)
{
    if (heap != NULL) {
        free(heap->items);
        heap_init(heap);
    }
}
