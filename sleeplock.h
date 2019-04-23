#ifndef SLEEPLOCK_H_
#define SLEEPLOCK_H_

// Long-term locks for processes
struct sleeplock {
  uint locked;       // Is the lock held?
  struct spinlock lk; // spinlock protecting this sleep lock
  
  // For debugging:
  char *name;        // Name of lock.
  int tid;           // thread holding lock
};

#endif