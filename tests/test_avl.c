#include <stddef.h>

#include "avl.h"

/* Verify visitor order. O(1) time and O(1) space per callback. */
static void count_train(const Train *train, void *user_data)
{
    unsigned *previous = user_data;
    if (train->number <= *previous) {
        *previous = 0;
    } else {
        *previous = train->number;
    }
}

/* Exercise AVL insertion, lookup, traversal and delete edge cases. O(n log n). */
int test_avl(void)
{
    TrainNode *root = NULL;
    Train train = {0};
    unsigned previous = 0;
    const unsigned numbers[] = {30, 10, 20, 50, 40, 60};
    size_t i;
    for (i = 0; i < sizeof(numbers) / sizeof(numbers[0]); ++i) {
        train.number = numbers[i];
        if (avl_insert_train(&root, &train) != RAIL_OK) {
            avl_destroy(root);
            return 0;
        }
    }
    avl_visit_trains(root, count_train, &previous);
    if (previous != 60 || avl_find_train(root, 40) == NULL ||
        avl_find_train(root, 99) != NULL ||
        avl_remove_train(&root, 30) != RAIL_OK ||
        avl_find_train(root, 30) != NULL ||
        avl_remove_train(&root, 30) != RAIL_ERR_NOT_FOUND) {
        avl_destroy(root);
        return 0;
    }
    avl_destroy(root);
    return 1;
}
