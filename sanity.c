#include "types.h"
#include "user.h"
#include "kthread.h"

void start() {
    sleep(100);
    printf(1, "thread %d waking up\n", kthread_id());

    kthread_exit();
    return;
}

int
main()
{
    printf(1, "sanity: start\n");
    for (int i = 0; i < 20; ++i) {
        void *stack = malloc(MAX_STACK_SIZE);
        int thread_id = kthread_create(start, stack);
        printf(1, "thread_id = %d\n", thread_id);
    }
    printf(1,"forking %d", fork());
    exit();
}
