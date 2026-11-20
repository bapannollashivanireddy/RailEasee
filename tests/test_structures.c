#include <string.h>

#include "dll.h"
#include "queue.h"
#include "segtree.h"
#include "stack.h"

/* Exercise queue, DLL, segment bitmap and undo-stack edge cases. O(n). */
int test_auxiliary_structures(void)
{
    RacQueue queue;
    BookingRecord first;
    BookingRecord second;
    BookingRecord third;
    BookingRecord *head = NULL;
    SeatBitmap bitmap;
    SystemContext context;
    Booking snapshot;
    Booking restored;
    UndoKind kind;
    memset(&queue, 0, sizeof(queue));
    memset(&bitmap, 0, sizeof(bitmap));
    memset(&first, 0, sizeof(first));
    memset(&second, 0, sizeof(second));
    memset(&third, 0, sizeof(third));
    memset(&context, 0, sizeof(context));
    memset(&snapshot, 0, sizeof(snapshot));
    first.booking.quota = RAIL_QUOTA_TATKAL;
    first.booking.timestamp = 1;
    second.booking.quota = RAIL_QUOTA_GENERAL;
    second.booking.timestamp = 2;
    third.booking.quota = RAIL_QUOTA_GENERAL;
    third.booking.timestamp = 3;
    if (queue_init(&queue, 2) != RAIL_OK ||
        queue_push(&queue, &first) != RAIL_OK ||
        queue_push(&queue, &second) != RAIL_OK ||
        queue_push(&queue, &third) != RAIL_ERR_FULL ||
        queue_pop(&queue) != &first || !queue_remove(&queue, &second) ||
        queue_pop(&queue) != NULL) {
        queue_destroy(&queue);
        return 0;
    }
    queue_destroy(&queue);
    wl_insert_ordered(&head, &first);
    wl_insert_ordered(&head, &third);
    wl_insert_ordered(&head, &second);
    if (head != &second || wl_position(head, &third) != 2) {
        return 0;
    }
    wl_remove(&head, &second);
    if (head != &third || wl_position(head, &first) != 2) {
        return 0;
    }
    wl_remove(&head, &third);
    wl_remove(&head, &first);
    if (head != NULL) {
        return 0;
    }
    if (seat_bitmap_init(&bitmap, 2, 3) != RAIL_OK ||
        seat_bitmap_occupy(&bitmap, 0, 0, 2) != RAIL_OK ||
        !seat_bitmap_available(&bitmap, 0, 2, 3) ||
        seat_bitmap_available(&bitmap, 0, 1, 3) ||
        seat_bitmap_find(&bitmap, 0, 2) != 1 ||
        seat_bitmap_release(&bitmap, 0, 0, 2) != RAIL_OK ||
        seat_bitmap_find(&bitmap, 0, 3) != 0) {
        seat_bitmap_destroy(&bitmap);
        return 0;
    }
    seat_bitmap_destroy(&bitmap);
    snapshot.timestamp = 44;
    if (undo_push(&context, UNDO_BOOKING, &snapshot) != RAIL_OK ||
        !undo_pop(&context, &kind, &restored) || kind != UNDO_BOOKING ||
        restored.timestamp != snapshot.timestamp ||
        undo_pop(&context, &kind, &restored)) {
        undo_destroy(&context);
        return 0;
    }
    undo_destroy(&context);
    return 1;
}
