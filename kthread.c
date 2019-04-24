#include "kthread.h"
#include "user.h"


typedef struct {
    int depth;
    int number_of_mutexes;
    int * mutex_ids;
} tree;

