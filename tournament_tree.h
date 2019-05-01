#ifndef ASS2_TOURNAMENT_TREE_H
#define ASS2_TOURNAMENT_TREE_H

#include "kthread.h"
#include "user.h"


typedef struct {
    int depth;
    int number_of_mutexes;
    int *mutex_ids;
    int mutex_id; // used to syncronize the access to ids_available
    int *ids_available; //used to store all threads who locked it
} trnmnt_tree;

void finish_releasing(const trnmnt_tree *tree, int ID);

int exp2(int exp) { return 1 << exp; }


int father(int id) {
    return (id - 1) / 2;
}

int left_son(int id) {
    return (2 * id) + 1;
}

int right_son(int id) {
    return (2 * id) + 2;
}

trnmnt_tree *trnmnt_tree_alloc(int depth) {
    if (depth <= 0) return 0;

    trnmnt_tree *tree = malloc(sizeof(trnmnt_tree));
    if (!tree) return 0;

    tree->depth = depth;
    tree->number_of_mutexes = exp2(depth) - 1;

    // allocate all mutexed
    tree->mutex_ids = malloc(sizeof(int) * tree->number_of_mutexes);
    if (!tree->mutex_ids) {
        free(tree);
        return 0;
    }

    tree->ids_available = malloc(sizeof(int) * exp2(depth));
    if (!tree->ids_available) {
        free(tree->ids_available);
        free(tree);
        return 0;
    }
    memset(tree->ids_available, 0, sizeof(int) * exp2(depth));
    tree->mutex_id = kthread_mutex_alloc();
    if (!tree->mutex_id) {
        free(tree->mutex_ids);
        free(tree->ids_available);
        free(tree);
    }
    // allocating mutexes, if one fails- clear all and return 0
    for (int i = 0; i < tree->number_of_mutexes; i++) {
        tree->mutex_ids[i] = kthread_mutex_alloc();
        if (tree->mutex_ids[i] == -1) { // if allocating a mutex failed
            for (int j = i; j >= 0; j++) {
                kthread_mutex_dealloc(tree->mutex_ids[j]);
            }
            kthread_mutex_dealloc(tree->mutex_id);
            free(tree->mutex_ids);
            free(tree->ids_available);
            free(tree);
            return 0;
        }
    }

    return tree;
}

int trnmnt_tree_dealloc(trnmnt_tree *tree) {
    kthread_mutex_lock(tree->mutex_id);
    for (int i = 0 ; i < exp2(tree->depth); i ++){//checking if any lock is still held, and if it is fail the operation
        if (tree->ids_available[i]) {
            kthread_mutex_unlock(tree->mutex_id);
            return -3;
        }
    }
    kthread_mutex_unlock(tree->mutex_id);
    for (int i = 0; i < tree->number_of_mutexes - 1; ++i) {
        kthread_mutex_dealloc(tree->mutex_ids[i]);
    }
    free(tree->mutex_ids);
    free(tree->ids_available);
    free(tree);
    return 0;
}

int acquire_helper(trnmnt_tree *tree, int ID) {
    if (ID >= tree->number_of_mutexes || ID < 0)// checking ID is in bounds
        return -3;
    int lock_result = kthread_mutex_lock(tree->mutex_ids[ID]);
    // if we have reached the last lock to be acquired or acquiring failed
    if (ID == 0 || lock_result < 0)
        return lock_result;
    return acquire_helper(tree, father(ID));
}

int get_first_lock_of_id(trnmnt_tree * tree, int ID){
    return ((exp2(tree->depth) -1) / 2) + (ID / 2);
}
int trnmnt_tree_acquire(trnmnt_tree *tree, int ID) {
    kthread_mutex_lock(tree->mutex_id);
    if (tree->ids_available[ID]) { // if ID is already taken
        kthread_mutex_unlock(tree->mutex_id);
        return -2;
    }
    tree->ids_available[ID] = 1;
    kthread_mutex_unlock(tree->mutex_id);

    int acquire_result = acquire_helper(tree, get_first_lock_of_id(tree, ID));
    // if for some reason acquire failed
    if (acquire_result < 0) {
        kthread_mutex_lock(tree->mutex_id);
        tree->ids_available[ID] = 0;
        kthread_mutex_unlock(tree->mutex_id);
    }
    return acquire_result;
}

//assuming lock at tree->ids_available[0] was already released
int release_helper(trnmnt_tree *tree, int original_id, int current_mutex_id, int current_depth) {
    if (tree->depth - 1 == current_depth){
        // if we have reached the original id- it means all relevant mutexes are released
        if (current_mutex_id == original_id)
            return 1;
        else
            return -5;
    }
    // now we know that the current depth is not the tree depth and that there is more where to go
    if (current_mutex_id >= tree->number_of_mutexes || current_mutex_id < 0)// if the current mutex id is out of bounds
        return -6;
    int left_son_released = kthread_mutex_unlock(tree->mutex_ids[left_son(current_mutex_id)]);
    int right_son_released = kthread_mutex_unlock(tree->mutex_ids[right_son(current_mutex_id)]);
    if (left_son_released < 0 && right_son_released < 0) // if both releases failed- fail the operation
        return -7;
    if (left_son_released >= 0)
        return release_helper(tree, original_id, left_son(current_mutex_id), current_depth + 1);
    return release_helper(tree, original_id, right_son(current_mutex_id), current_depth + 1);
}

void finish_releasing(const trnmnt_tree *tree, int ID) {
    kthread_mutex_lock(tree->mutex_id);
    tree->ids_available[ID] = 0;
    kthread_mutex_unlock(tree->mutex_id);
}

int trnmnt_tree_release(trnmnt_tree *tree, int ID) {
    kthread_mutex_lock(tree->mutex_id);
    if (tree->ids_available[ID] == 0) { // if ID is already taken
        kthread_mutex_unlock(tree->mutex_id);
        return -1;
    }
    kthread_mutex_unlock(tree->mutex_id);
    // if ID not legal
    if (ID < 0 || ID >= exp2(tree->depth))
        return -2;
    if (kthread_mutex_unlock(tree->mutex_ids[0]) == 0) {// if managed to free the first lock
        // if the tree is of depth 1:
        // there is only one lock, and therefore we can free the tree now
        if (tree->depth == 1){
            finish_releasing(tree, ID);
            return 1;
        }
        int release_result = release_helper(tree, get_first_lock_of_id(tree, ID), 0, 0);
        if (release_result == 0) {
            finish_releasing(tree, ID);
        }
        return release_result;
    }
    return -3;
}



#endif //ASS2_TOURNAMENT_TREE_H
