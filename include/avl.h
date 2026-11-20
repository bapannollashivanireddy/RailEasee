#ifndef RAIL_EASE_AVL_H
#define RAIL_EASE_AVL_H

#include "models.h"

typedef struct TrainNode {
    uint64_t key;
    int height;
    bool history_entry;
    Train train;
    Booking booking;
    struct TrainNode *left;
    struct TrainNode *right;
} TrainNode;

/* Find train; params: root, number. Returns node/NULL. O(log n), O(1) space. */
const TrainNode *avl_find_train(const TrainNode *root, unsigned number);

/* Insert/replace train; params: root, value. Returns error. O(log n), O(log n) stack. */
RailError avl_insert_train(TrainNode **root, const Train *train);

/* Remove train; params: root, number. Returns error. O(log n), O(log n) stack. */
RailError avl_remove_train(TrainNode **root, unsigned number);

/* Insert history; params: root, date key, booking. Returns error. O(log n), O(log n) stack. */
RailError avl_insert_history(TrainNode **root, uint64_t key, const Booking *booking);

/* In-order train traversal; params: root, visitor, user data. Returns nothing. O(n). */
void avl_visit_trains(const TrainNode *root, void (*visit)(const Train *, void *),
                      void *user_data);

/* In-order history traversal; params: root, visitor, user data. Returns nothing. O(n). */
void avl_visit_history(const TrainNode *root,
                       void (*visit)(uint64_t, const Booking *, void *),
                       void *user_data);

/* Free tree; params: root. Returns nothing. O(n) time, O(log n) stack. */
void avl_destroy(TrainNode *root);

#endif
