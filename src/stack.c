#include <stdlib.h>

#include "stack.h"

/* Push a copy of the last action. O(1) time and O(1) space. */
RailError undo_push(SystemContext *context, UndoKind kind, const Booking *booking)
{
    UndoNode *node;
    if (context == NULL || booking == NULL) {
        return RAIL_ERR_INVALID;
    }
    node = malloc(sizeof(*node));
    if (node == NULL) {
        return RAIL_ERR_MEMORY;
    }
    node->kind = kind;
    node->booking = *booking;
    node->next = context->undo_top;
    context->undo_top = node;
    return RAIL_OK;
}

/* Pop the newest action. O(1) time and O(1) space. */
bool undo_pop(SystemContext *context, UndoKind *kind, Booking *booking)
{
    UndoNode *node;
    if (context == NULL || context->undo_top == NULL ||
        kind == NULL || booking == NULL) {
        return false;
    }
    node = context->undo_top;
    context->undo_top = node->next;
    *kind = node->kind;
    *booking = node->booking;
    free(node);
    return true;
}

/* Free action snapshots. O(n) time and O(1) auxiliary space. */
void undo_destroy(SystemContext *context)
{
    if (context == NULL) {
        return;
    }
    while (context->undo_top != NULL) {
        UndoNode *next = context->undo_top->next;
        free(context->undo_top);
        context->undo_top = next;
    }
}
