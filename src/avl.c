#include <stdlib.h>
#include <string.h>

#include "avl.h"

/* Read a subtree height. O(1) time and O(1) space. */
static int node_height(const TrainNode *node)
{
    return node == NULL ? 0 : node->height;
}

/* Return the larger height. O(1) time and O(1) space. */
static int max_int(int a, int b)
{
    return a > b ? a : b;
}

/* Recompute one node height. O(1) time and O(1) space. */
static void update_height(TrainNode *node)
{
    node->height = 1 + max_int(node_height(node->left), node_height(node->right));
}

/* Apply a right AVL rotation. O(1) time and O(1) space. */
static TrainNode *rotate_right(TrainNode *root)
{
    TrainNode *pivot = root->left;
    root->left = pivot->right;
    pivot->right = root;
    update_height(root);
    update_height(pivot);
    return pivot;
}

/* Apply a left AVL rotation. O(1) time and O(1) space. */
static TrainNode *rotate_left(TrainNode *root)
{
    TrainNode *pivot = root->right;
    root->right = pivot->left;
    pivot->left = root;
    update_height(root);
    update_height(pivot);
    return pivot;
}

/* Rebalance one AVL subtree. O(1) time and O(1) space. */
static TrainNode *balance(TrainNode *root)
{
    int difference;
    if (root == NULL) {
        return NULL;
    }
    update_height(root);
    difference = node_height(root->left) - node_height(root->right);
    if (difference > 1) {
        if (node_height(root->left->right) > node_height(root->left->left)) {
            root->left = rotate_left(root->left);
        }
        return rotate_right(root);
    }
    if (difference < -1) {
        if (node_height(root->right->left) > node_height(root->right->right)) {
            root->right = rotate_right(root->right);
        }
        return rotate_left(root);
    }
    return root;
}

/* Allocate an initialized AVL node. O(1) time and O(1) space. */
static TrainNode *new_node(uint64_t key, bool history)
{
    TrainNode *node = calloc(1, sizeof(*node));
    if (node != NULL) {
        node->key = key;
        node->height = 1;
        node->history_entry = history;
    }
    return node;
}

/* Recursively insert/replace one train. O(log n) time and stack space. */
static TrainNode *insert_train_node(TrainNode *root, const Train *train,
                                    RailError *error)
{
    TrainNode *created;
    if (root == NULL) {
        created = new_node(train->number, false);
        if (created == NULL) {
            *error = RAIL_ERR_MEMORY;
            return NULL;
        }
        created->train = *train;
        *error = RAIL_OK;
        return created;
    }
    if (train->number < root->key) {
        root->left = insert_train_node(root->left, train, error);
    } else if (train->number > root->key) {
        root->right = insert_train_node(root->right, train, error);
    } else {
        root->train = *train;
        *error = RAIL_OK;
        return root;
    }
    return *error == RAIL_OK ? balance(root) : root;
}

/* Recursively insert/replace one history record. O(log n) time and stack. */
static TrainNode *insert_history_node(TrainNode *root, uint64_t key,
                                      const Booking *booking, RailError *error)
{
    TrainNode *created;
    if (root == NULL) {
        created = new_node(key, true);
        if (created == NULL) {
            *error = RAIL_ERR_MEMORY;
            return NULL;
        }
        created->booking = *booking;
        *error = RAIL_OK;
        return created;
    }
    if (key < root->key) {
        root->left = insert_history_node(root->left, key, booking, error);
    } else if (key > root->key) {
        root->right = insert_history_node(root->right, key, booking, error);
    } else {
        root->booking = *booking;
        *error = RAIL_OK;
        return root;
    }
    return *error == RAIL_OK ? balance(root) : root;
}

/* Find a train. O(log n) time and O(1) space. */
const TrainNode *avl_find_train(const TrainNode *root, unsigned number)
{
    while (root != NULL && root->key != number) {
        root = number < root->key ? root->left : root->right;
    }
    return root == NULL || root->history_entry ? NULL : root;
}

/* Insert or replace a train. O(log n) time and O(log n) stack. */
RailError avl_insert_train(TrainNode **root, const Train *train)
{
    RailError error = RAIL_OK;
    if (root == NULL || train == NULL || train->number == 0) {
        return RAIL_ERR_INVALID;
    }
    *root = insert_train_node(*root, train, &error);
    return error;
}

/* Detach the smallest key. O(log n) time and stack space. */
static TrainNode *detach_min(TrainNode *root, TrainNode **minimum)
{
    if (root->left == NULL) {
        *minimum = root;
        return root->right;
    }
    root->left = detach_min(root->left, minimum);
    return balance(root);
}

/* Delete and rebalance a key. O(log n) time and stack space. */
static TrainNode *remove_node(TrainNode *root, uint64_t key, bool *found)
{
    TrainNode *successor;
    if (root == NULL) {
        return NULL;
    }
    if (key < root->key) {
        root->left = remove_node(root->left, key, found);
    } else if (key > root->key) {
        root->right = remove_node(root->right, key, found);
    } else {
        *found = true;
        if (root->left == NULL || root->right == NULL) {
            TrainNode *child = root->left != NULL ? root->left : root->right;
            free(root);
            return child;
        }
        root->right = detach_min(root->right, &successor);
        successor->left = root->left;
        successor->right = root->right;
        free(root);
        root = successor;
    }
    return balance(root);
}

/* Remove a train. O(log n) time and O(log n) stack. */
RailError avl_remove_train(TrainNode **root, unsigned number)
{
    bool found = false;
    if (root == NULL || number == 0) {
        return RAIL_ERR_INVALID;
    }
    *root = remove_node(*root, number, &found);
    return found ? RAIL_OK : RAIL_ERR_NOT_FOUND;
}

/* Insert a date-keyed booking history item. O(log n) time and stack. */
RailError avl_insert_history(TrainNode **root, uint64_t key, const Booking *booking)
{
    RailError error = RAIL_OK;
    if (root == NULL || booking == NULL) {
        return RAIL_ERR_INVALID;
    }
    *root = insert_history_node(*root, key, booking, &error);
    return error;
}

/* Traverse trains in key order. O(n) time and O(log n) stack. */
void avl_visit_trains(const TrainNode *root, void (*visit)(const Train *, void *),
                      void *user_data)
{
    if (root == NULL) {
        return;
    }
    avl_visit_trains(root->left, visit, user_data);
    if (!root->history_entry && visit != NULL) {
        visit(&root->train, user_data);
    }
    avl_visit_trains(root->right, visit, user_data);
}

/* Traverse booking history in key order. O(n) time and O(log n) stack. */
void avl_visit_history(const TrainNode *root,
                       void (*visit)(uint64_t, const Booking *, void *),
                       void *user_data)
{
    if (root == NULL) {
        return;
    }
    avl_visit_history(root->left, visit, user_data);
    if (root->history_entry && visit != NULL) {
        visit(root->key, &root->booking, user_data);
    }
    avl_visit_history(root->right, visit, user_data);
}

/* Free an AVL tree. O(n) time and O(log n) stack. */
void avl_destroy(TrainNode *root)
{
    if (root != NULL) {
        avl_destroy(root->left);
        avl_destroy(root->right);
        free(root);
    }
}
