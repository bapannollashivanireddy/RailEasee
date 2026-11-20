#ifndef RAIL_EASE_STACK_H
#define RAIL_EASE_STACK_H

#include "models.h"

typedef enum {
    UNDO_BOOKING = 0,
    UNDO_CANCELLATION
} UndoKind;

typedef struct UndoNode {
    UndoKind kind;
    Booking booking;
    struct UndoNode *next;
} UndoNode;

/* Push snapshot; params: context, action kind, booking. Returns error. O(1) time. */
RailError undo_push(SystemContext *context, UndoKind kind, const Booking *booking);

/* Pop snapshot; params: context, output kind/booking. Returns found. O(1) time. */
bool undo_pop(SystemContext *context, UndoKind *kind, Booking *booking);

/* Destroy stack; params: context. Returns nothing. O(n) time, O(1) space. */
void undo_destroy(SystemContext *context);

#endif
