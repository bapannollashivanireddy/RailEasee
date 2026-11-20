#include <string.h>

#include "heap.h"

/* Exercise priority order, arbitrary removal and the empty edge. O(log n). */
int test_heap(void)
{
    BookingHeap heap;
    BookingRecord records[4];
    memset(records, 0, sizeof(records));
    heap_init(&heap);
    records[0].booking.quota = RAIL_QUOTA_GENERAL;
    records[0].booking.timestamp = 30;
    records[1].booking.quota = RAIL_QUOTA_TATKAL;
    records[1].booking.timestamp = 1;
    records[2].booking.quota = RAIL_QUOTA_GENERAL;
    records[2].booking.timestamp = 10;
    records[3].booking.quota = RAIL_QUOTA_GENERAL;
    records[3].booking.timestamp = 20;
    if (heap_push(&heap, &records[0]) != RAIL_OK ||
        heap_push(&heap, &records[1]) != RAIL_OK ||
        heap_push(&heap, &records[2]) != RAIL_OK ||
        heap_push(&heap, &records[3]) != RAIL_OK ||
        heap_peek(&heap) != &records[2] ||
        heap_decrease_key(&heap, &records[1], RAIL_QUOTA_GENERAL, 5) != RAIL_OK ||
        heap_peek(&heap) != &records[1] ||
        heap_decrease_key(&heap, &records[1], RAIL_QUOTA_TATKAL, 10) != RAIL_ERR_INVALID ||
        heap_remove(&heap, &records[0]) != &records[0] ||
        records[0].heap_index != SIZE_MAX ||
        heap_pop(&heap) != &records[1] ||
        heap_pop(&heap) != &records[2] ||
        heap_pop(&heap) != &records[3] ||
        heap_pop(&heap) != NULL) {
        heap_destroy(&heap);
        return 0;
    }
    heap_destroy(&heap);
    return heap.size == 0;
}
