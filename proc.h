#include "param.h"
#include "spinlock.h"
#include "sleeplock.h"

// Per-CPU state
struct cpu {
    uchar apicid;                // Local APIC ID
    struct context *scheduler;   // swtch() here to enter scheduler
    struct taskstate ts;         // Used by x86 to find stack for interrupt
    struct segdesc gdt[NSEGS];   // x86 global descriptor table
    volatile uint started;       // Has the CPU started?
    int ncli;                    // Depth of pushcli nesting.
    int intena;                  // Were interrupts enabled before pushcli?
    struct thread *thread;       // The thread running on this cpu or null
    struct proc * proc;
};

extern struct cpu cpus[NCPU];
extern int ncpu;

//PAGEBREAK: 17
// Saved registers for kernel context switches.
// Don't need to save all the segment registers (%cs, etc),
// because they are constant across kernel contexts.
// Don't need to save %eax, %ecx, %edx, because the
// x86 convention is that the caller has saved them.
// Contexts are stored at the bottom of the stack they
// describe; the stack pointer is the address of the context.
// The layout of the context matches the layout of the stack in swtch.S
// at the "Switch stacks" comment. Switch doesn't save eip explicitly,
// but it is on the stack and allocproc() manipulates it.
struct context {
    uint edi;
    uint esi;
    uint ebx;
    uint ebp;
    uint eip;
};

enum procstate { UNUSED, EMBRYO, USED, ZOMBIE };
enum threadstate { T_UNUSED, T_RUNNABLE, T_RUNNING, T_SLEEPING , T_TERMINATED};
enum mutexstate {M_UNUSED,M_USED};

// Process memory is laid out contiguously, low addresses first:
//   text
//   original data and bss
//   fixed-size stack
//   expandable heap
struct thread {
    char *kstack;                // Bottom of kernel stack for this thread
    enum threadstate t_state;   // Thread state
    volatile int tid;                     // Thread ID
    struct trapframe *tf;        // Trap frame for current syscall
    struct context *context;     // swtch() here to run process
    void *chan;                  // If non-zero, sleeping on chan
    struct proc *proc;           // process
    int killed;
};

// Per-process state
struct proc {
    uint sz;                     // Size of process memory (bytes)
    pde_t *pgdir;                // Page table
    enum procstate state;        // Process state
    int pid;                     // Process ID
    struct proc *parent;         // Parent process
    int killed;                  // If non-zero, have been killed
    struct file *ofile[NOFILE];  // Open files
    struct inode *cwd;           // Current directory
    char name[16];               // Process name (debugging)
    struct spinlock proclock;    // Lock in the process context
    struct thread threads[NTHREADS]; //threads of the process
};

typedef struct {
    struct sleeplock lock;
    int id;
    enum mutexstate m_state;
}pthread_mutex_t;
