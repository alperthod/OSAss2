#include "types.h"
#include "user.h"
#include "kthread.h"
#include "tournament_tree.h"

int mutex_id;

int counter;

/**************************
 *         Test 1         *
 **************************/

void start() {
    counter++;
    kthread_exit();
}

/**
 * Sanity testing that spawning threads works, and that they do their job
 */

static int print_mutex;
void
sanity_thread_test()
{
    counter = 0;
    int nthreads = 15;
    int * thread_ids = malloc(sizeof(int) * nthreads);

    for (int i = 0; i < nthreads; ++i) {
        thread_ids[i] = kthread_create(start, malloc(MAX_STACK_SIZE));
    }

    for (int i = 0; i < nthreads; ++i) {
        kthread_join(thread_ids[i]);
    }

    // Counter might be less than 15 - we haven't used mutexes
    // and we may have race conditions
    if (counter > 0 && counter <= 15)
        printf(1, "sanity test SUCCESS\n");
    else
        printf(1, "sanity test FAILURE\n");
}

/**************************
 *         Test 2         *
 **************************/

void start2() {
    kthread_mutex_lock(mutex_id);
    int tmp = counter;
    // Sleep to allow for race conditions if there are any
    sleep(10);
    counter = tmp + 1;
    kthread_mutex_unlock(mutex_id);
    kthread_exit();
}

/**
 * Testing that creating threads in different processes work. Also, test mutexes and use then to sync
 * access to a counter.
 * This should print SUCCESS twice.
 */
void
test_processes_and_mutexes()
{
    mutex_id = kthread_mutex_alloc();
    counter = 0;
    int nthreads = 15;

    int forkret = fork();

    int * thread_ids = malloc(sizeof(int) * nthreads);

    for (int i = 0; i < nthreads; ++i) {
        thread_ids[i] = kthread_create(start2, malloc(MAX_STACK_SIZE));
    }

    for (int i = 0; i < nthreads; ++i) {
        kthread_join(thread_ids[i]);
    }

    kthread_mutex_lock(mutex_id);

    if (counter == 15) {
        printf(1, "fork and mutex test SUCCESS\n");
    }
    else {
        printf(1, "fork and mutex test FAILURE\n");
    }

    kthread_mutex_unlock(mutex_id);

    if (forkret > 0) {
        wait();
    } else {
        exit();
    }

    kthread_mutex_dealloc(mutex_id);
}

/**************************
 *         Test 3         *
 **************************/

trnmnt_tree* tree;
int * thread_ids;

void tree_thread() {
    int thread_id = kthread_id();
    int thread_index = 0;

    // Find our thread index
    for (int i = 0; i < 8; ++i) {
        if (thread_ids[i] == thread_id) {
            thread_index = i;
        }
    }
    kthread_mutex_lock(print_mutex);
    printf(1, "thread index: %d, acquire_result: %d\n", thread_index, trnmnt_tree_acquire(tree, thread_index));
    kthread_mutex_unlock(print_mutex);
    int tmp = counter;
    // Sleep to allow race conditions if they are any
    sleep(10);
    counter = tmp + 1;

    trnmnt_tree_release(tree, thread_index);

    kthread_exit();
}

void print_tree(trnmnt_tree * tree){
    printf(1, "tree->depth: %d\n", tree->depth);
    printf(1, "tree->number_of_mutexes: %d\n", tree->number_of_mutexes);
    printf(1, "tree->mutex_ids->[");
    for (int i = 0; i< tree->number_of_mutexes; i++){
        printf(1, "%d, ",tree->mutex_ids[i]);
    }
    printf(1,"]\n");
    printf(1, "tree->mutex_id: %d\n", tree->mutex_id);
    printf(1, "tree->number_of_ids_available: %d\n", exp2(tree->depth));

}
/**
 * Testing the tournament tree.
 * Creating a tree with depth = 3, running 8 threads, and checking the mutual exclusion is preserved.
 */
void trnmnt_tree_test() {
    counter = 0;
    int nthreads = 8;
    tree = trnmnt_tree_alloc(3);
    print_tree(tree);
    thread_ids = malloc(nthreads * sizeof(int));
    memset(thread_ids, 0, nthreads * sizeof(int));

    for (int i = 0; i < nthreads; ++i) {
        thread_ids[i] = kthread_create(tree_thread, malloc(MAX_STACK_SIZE));
    }

    for (int i = 0; i < nthreads; ++i) {
        kthread_join(thread_ids[i]);
    }

    trnmnt_tree_dealloc(tree);

    if (counter == nthreads)
        printf(1, "trnmt tree test SUCCESS\n");
    else
        printf(1, "trnmt tree test FAILURE\n");
}

int main() {
    sanity_thread_test();
    test_processes_and_mutexes();
    print_mutex = kthread_mutex_alloc();
    trnmnt_tree_test();
    exit();
}
