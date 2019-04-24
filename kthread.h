#ifndef KTHREAD_H_
#define KTHREAD_H_

#include "user.h"

#define MAX_STACK_SIZE 4000
#define MAX_MUTEXES 64

/********************************
        The API of the KLT package
 ********************************/
int exp2(int exp) { return 1 << exp; }


typedef struct {
    int depth;
    int number_of_mutexes;
    int * mutex_ids;
} trnmnt_tree;

int kthread_create(void (*start_func)(), void* stack);
int kthread_id();
void kthread_exit();
int kthread_join(int thread_id);

int kthread_mutex_alloc();
int kthread_mutex_dealloc(int mutex_id);
int kthread_mutex_lock(int mutex_id);
int kthread_mutex_unlock(int mutex_id);

int father(int id){
    return id / 2;
}

trnmnt_tree* trnmnt_tree_alloc(int depth) {
    if (depth <= 0) return 0;

    trnmnt_tree* tree = malloc(sizeof(trnmnt_tree));
    if (!tree) return 0;

    // malloc all nodes
    tree->depth = depth;
    tree->number_of_mutexes = exp2(depth-1);
    tree->mutex_ids = malloc(sizeof(int) * tree->number_of_mutexes);

    // allocate all mutexed
    tree->mutex_ids = malloc(sizeof(int) * tree->number_of_mutexes);
    if (!tree->mutex_ids){
        free(tree);
        return 0;
    }

    // allocating mutexes, if one fails- clear all and return 0
    for (int i = 0; i < tree->number_of_mutexes - 1; ++i) {
        tree->mutex_ids[i] = kthread_mutex_alloc();
        if (tree->mutex_ids[i] == -1){ // if allocating a mutex failed
            for (int j = i; j >= 0; j++){
                kthread_mutex_dealloc(tree->mutex_ids[j]);
            }
            free(tree->mutex_ids);
            free(tree);
            return 0;
        }
    }

    return tree;
}
int trnmnt_tree_dealloc(trnmnt_tree* tree);
int trnmnt_tree_acquire(trnmnt_tree* tree,int ID);
int trnmnt_tree_release(trnmnt_tree* tree,int ID);



#endif