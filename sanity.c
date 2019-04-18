#include "types.h"
#include "user.h"
#include "kthread.h"

void start() {
    printf(1, "in thread %d\n", kthread_id());
    kthread_exit();
    return;
}

int
main()
{
    int pid;
    printf(1, "sanity: start\n");
    for (int j = 0 ; j < 5 ; j++){
    pid = fork();
    for (int i = 0; i < 20; ++i) {
        void *stack = malloc(MAX_STACK_SIZE);
        int thread_id = kthread_create(start, stack);
        kthread_join(thread_id);
        printf(1, "proc_id = %d, thread_id = %d\n",pid, thread_id);
    }
    if (pid)
        wait();
    }
//  kthread_join(thread_id);
    exit();
}
