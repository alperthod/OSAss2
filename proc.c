#include "types.h"
#include "defs.h"
#include "param.h"
#include "memlayout.h"
#include "mmu.h"
#include "x86.h"
#include "proc.h"
#include "spinlock.h"
#include "kthread.h"

struct {
  struct spinlock lock;
  struct proc proc[NPROC];
} ptable;

struct{
    struct spinlock lock;
    pthread_mutex_t mutexes[MAX_MUTEXES];
}mtable;


static struct proc *initproc;

int nextpid = 1;
int nextmid = 1;
extern void forkret(void);
extern void trapret(void);
int is_last_thread();
static void wakeup1(void *chan);

void
pinit(void)
{
  initlock(&ptable.lock, "ptable");
}

// Must be called with interrupts disabled
int
cpuid() {
  return mycpu()-cpus;
}

// Must be called with interrupts disabled to avoid the caller being
// rescheduled between reading lapicid and running through the loop.
struct cpu*
mycpu(void)
{
  int apicid, i;

  if(readeflags()&FL_IF)
    panic("mycpu called with interrupts enabled\n");

  apicid = lapicid();
  // APIC IDs are not guaranteed to be contiguous. Maybe we should have
  // a reverse map, or reserve a register to store &cpus[i].
  for (i = 0; i < ncpu; ++i) {
    if (cpus[i].apicid == apicid)
      return &cpus[i];
  }
  panic("unknown apicid\n");
}

// Disable interrupts so that we are not rescheduled
// while reading thread from the cpu structure
struct thread* my_thread(void) {
    struct cpu *c;
    struct thread *t;
    pushcli();
    c = mycpu();
    t = c->thread;
    popcli();
    return t;
}

// Disable interrupts so that we are not rescheduled
// while reading proc from the cpu structure
struct proc*
myproc(void) {
  struct thread * t = my_thread();
  return t == 0 ? 0 : t->proc;
}
int remaining_threads(struct proc * proc) {
    int ans = 0;
    struct thread * t;
    for (t = proc->threads; t < &proc->threads[NTHREADS]; t++){
        if (t->t_state != T_UNUSED && t->t_state != T_TERMINATED) ans++;
    }
    return ans;
}

void kill_other_threads() {
    struct proc *curproc = myproc();

    acquire(&curproc->proclock);
    curproc->killed = 1;

    // While we are not the only thread running
    while (remaining_threads(curproc) > 1) {
        // Wait for a dying thread to wake us up
        sleep(curproc, &curproc->proclock);
    }
    release(&curproc->proclock);

    // Now our thread is running alone, we can put it back to normal
    curproc->killed = 0;
}
//assuming proclock is held
static struct thread* alloc_thread(struct proc* proc) {
    if (!holding(&proc->proclock))
        panic("proclock must be held while allocating thread");
    struct thread * t;
    for (t = proc->threads; t < &proc->threads[NTHREADS]; t++) {
        if (t->t_state != T_UNUSED && t->t_state!= T_TERMINATED)
            continue;
        t->tid = nextpid++;
        char *sp;

        // Allocate kernel stack.
        if((t->kstack = kalloc()) == 0) {
            return 0;
        }
        sp = t->kstack + KSTACKSIZE;

        // Leave room for trap frame.
        sp -= sizeof *t->tf;
        t->tf = (struct trapframe*)sp;

        // Set up new context to start executing at forkret,
        // which returns to trapret.
        sp -= 4;
        *(uint*)sp = (uint)trapret;

        sp -= sizeof *t->context;
        t->context = (struct context*)sp;
        memset(t->context, 0, sizeof *t->context);
        t->context->eip = (uint)forkret;
        t->chan = 0;
        return t;
    }
    return 0;
}
//PAGEBREAK: 32
// Look in the process table for an UNUSED proc.
// If found, change state to EMBRYO and initialize
// state required to run in the kernel.
// Otherwise return 0.
static struct proc*
allocproc(void)
{
  struct proc *p;
  acquire(&ptable.lock);

  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++)
    if(p->state == UNUSED)
      goto found;

  release(&ptable.lock);
  return 0;

found:
  p->state = EMBRYO;
  p->pid = nextpid++;
// initializing threads for the process
  for (int i = 0 ; i < NTHREADS ; i++) {
      p->threads[i].t_state = T_UNUSED;
      p->threads[i].proc = p;
  }
// initializing proclock
  initlock(&p->proclock, "process lock");
  acquire(&p->proclock);
  struct thread * t = alloc_thread(p);
  release(&p->proclock);
    if (t == 0){
      release(&ptable.lock);
      return 0;
  }
  p->state = USED;
  release(&ptable.lock);
  return p;
}

//PAGEBREAK: 32
// Set up first user process.
void
userinit(void)
{
  struct thread *thread;
  struct proc * proc;
  extern char _binary_initcode_start[], _binary_initcode_size[];

  proc = allocproc();
  thread = &proc->threads[0];

  initproc = proc;
  if((proc->pgdir = setupkvm()) == 0)
    panic("userinit: out of memory?");
  inituvm(proc->pgdir, _binary_initcode_start, (int)_binary_initcode_size);
  proc->sz = PGSIZE;
  memset(thread->tf, 0, sizeof(*thread->tf));
  thread->tf->cs = (SEG_UCODE << 3) | DPL_USER;
  thread->tf->ds = (SEG_UDATA << 3) | DPL_USER;
  thread->tf->es = thread->tf->ds;
  thread->tf->ss = thread->tf->ds;
  thread->tf->eflags = FL_IF;
  thread->tf->esp = PGSIZE;
  thread->tf->eip = 0;  // beginning of initcode.S

  safestrcpy(proc->name, "initcode", sizeof(proc->name));
  proc->cwd = namei("/");

  // this assignment to p->state lets other cores
  // run this process. the acquire forces the above
  // writes to be visible, and the lock is also needed
  // because the assignment might not be atomic.
  acquire(&ptable.lock);
  acquire(&proc->proclock);
  thread->t_state = T_RUNNABLE;
  release(&proc->proclock);
  release(&ptable.lock);
}

// Grow current process's memory by n bytes.
// Return 0 on success, -1 on failure.
int
growproc(int n)
{

  uint sz;
  struct proc *curproc = myproc();
  struct thread* curr_thread = my_thread();
  acquire(&curproc->proclock);
  sz = curproc->sz;
  if(n > 0){
    if((sz = allocuvm(curproc->pgdir, sz, sz + n)) == 0) {
        release(&curproc->proclock);
        return -1;
    }
  } else if(n < 0){
    if((sz = deallocuvm(curproc->pgdir, sz, sz + n)) == 0) {
        release(&curproc->proclock);
        return -1;
    }
  }
  curproc->sz = sz;
  switchuvm(curr_thread);
  release(&curproc->proclock);
  return 0;
}

// Create a new process copying p as the parent.
// Sets up stack to return as if from system call.
// Caller must set state of returned proc to RUNNABLE.
int
fork(void)
{
  int i, pid;
  struct thread *new_thread;
  struct proc * new_proc;
  struct thread *curr_thread = my_thread();
  // Allocate process.
    if((new_proc = allocproc()) == 0) {
        return -1;
    }
    new_thread = &new_proc->threads[0];
  acquire(&ptable.lock);
  acquire(&curr_thread->proc->proclock);
    // Copy process state from proc.
  if((new_thread->proc->pgdir = copyuvm(curr_thread->proc->pgdir, curr_thread->proc->sz)) == 0){
    kfree(new_thread->kstack);
    new_thread->kstack = 0;
    new_thread->t_state = T_UNUSED;
    release(&curr_thread->proc->proclock);
    release(&ptable.lock);
    return -1;
  }
  new_thread->proc->sz = curr_thread->proc->sz;
  new_thread->proc->parent = curr_thread->proc;
  *new_thread->tf = *curr_thread->tf;

  // Clear %eax so that fork returns 0 in the child.
  new_thread->tf->eax = 0;

  for(i = 0; i < NOFILE; i++) {
      if (curr_thread->proc->ofile[i])
          new_thread->proc->ofile[i] = filedup(curr_thread->proc->ofile[i]);
  }
  new_thread->proc->cwd = idup(curr_thread->proc->cwd);

  safestrcpy(new_thread->proc->name, curr_thread->proc->name, sizeof(curr_thread->proc->name));

  pid = new_thread->proc->pid;
  new_thread->t_state = T_RUNNABLE;
  new_thread->proc->state = USED;
  release(&curr_thread->proc->proclock);
  release(&ptable.lock);
  return pid;
}

// Exit the current process.  Does not return.
// An exited process remains in the zombie state
// until its parent calls wait() to find out it exited.
void
exit(void)
{
  struct proc *curproc = myproc();
  struct thread * curr_thread = my_thread();
  struct proc *p;
  int fd;
  //if this is the first thread that's get exit call need to signal all other threads to exit
  if(curproc == initproc)
      panic("init exiting");

    acquire(&curproc->proclock);
    if (!curr_thread->killed) {
        curproc->killed = 1;

    }

  if (is_last_thread()) {
      release(&curproc->proclock);
    // Close all open files.
    for (fd = 0; fd < NOFILE; fd++) {
        if (curproc->ofile[fd]) {
            fileclose(curproc->ofile[fd]);
            curproc->ofile[fd] = 0;
        }
    }

  begin_op();
  iput(curproc->cwd);
  end_op();
  curproc->cwd = 0;

  acquire(&ptable.lock);

  // Parent might be sleeping in wait().
  wakeup1(curproc->parent);

  // Pass abandoned children to init.
  for (p = ptable.proc; p < &ptable.proc[NPROC]; p++) {
      if (p->parent == curproc) {
          p->parent = initproc;
          if (p->state == ZOMBIE)
              wakeup1(initproc);
      }
  }

  // Jump into the scheduler, never to return.
  acquire(&curproc->proclock);
  curproc->state = ZOMBIE;
  curr_thread->t_state = T_TERMINATED;
  sched();
  panic("zombie exit");
    }
  else {
      release(&curproc->proclock);
      kthread_exit();
    }
}

// Wait for a child process to exit and return its pid.
// Return -1 if this process has no children.
int
wait(void)
{
  struct proc *p;
  int havekids, pid;
  struct thread * thread;
  struct proc *curproc = myproc();

  acquire(&ptable.lock);
  for(;;){
    // Scan through table looking for exited children.
    havekids = 0;
    for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
      if(p->parent != curproc)
        continue;
      havekids = 1;
      if(p->state == ZOMBIE){
        // Found one.
        pid = p->pid;
        acquire(&p->proclock);
        for (thread = p->threads ; thread < &p->threads[NTHREADS] ; thread++){
            if(thread->t_state != T_UNUSED) {
                kfree(thread->kstack);
                thread->kstack = 0;
                thread->t_state = T_UNUSED;
                thread->proc = 0;
            }
        }

        freevm(p->pgdir);
        p->pid = 0;
        p->parent = 0;
        p->name[0] = 0;
        p->killed = 0;
        p->state = UNUSED;
        release(&p->proclock);
        release(&ptable.lock);
        return pid;
      }
    }

    // No point waiting if we don't have any children.
    if(!havekids || curproc->killed){
      release(&ptable.lock);
      return -1;
    }
    // Wait for children to exit.  (See wakeup1 call in proc_exit.)
    sleep(curproc, &ptable.lock);  //DOC: wait-sleep
  }
}

//PAGEBREAK: 42
// Per-CPU process scheduler.
// Each CPU calls scheduler() after setting itself up.
// Scheduler never returns.  It loops, doing:
//  - choose a process to run
//  - swtch to start running that process
//  - eventually that process transfers control
//      via swtch back to the scheduler.
void
scheduler(void)
{
  struct proc *p;
  struct thread * t;
  struct cpu *c = mycpu();
  c->thread = 0;

  for(;;){
    // Enable interrupts on this processor.
    sti();

    // Loop over process table looking for process to run.
    acquire(&ptable.lock);
    for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
      if(p->state != USED)
        continue;
      c->proc = p;
      acquire(&p->proclock);
      for (t = p->threads ; t < &p->threads[NTHREADS] ; t++) {
          if (t->t_state != T_RUNNABLE)
              continue;
          // Switch to chosen thread.  It is the thread's job
          // to release ptable and the process's lock and then reacquire it
          // before jumping back to us.
          c->thread = t;
          switchuvm(t);
          t->t_state = T_RUNNING;

          swtch(&(c->scheduler), t->context);
          switchkvm();

          // Process is done running for now.
          // It should have changed its p->state before coming back.
          c->thread = 0;
      }
        release(&p->proclock);
        c->proc = 0;
    }
    release(&ptable.lock);

  }
}

// Enter scheduler.  Must hold only ptable.lock
// and have changed proc->state. Saves and restores
// intena because intena is a property of this
// kernel thread, not this CPU. It should
// be proc->intena and proc->ncli, but that would
// break in the few places where a lock is held but
// there's no process.
void
sched(void)
{
  int intena;
  struct thread * t = my_thread();

  if(!holding(&ptable.lock))
    panic("sched ptable.lock");
  if(!holding(&t->proc->proclock))
    panic("sched proc lock must be held");
  if(mycpu()->ncli != 2)
    panic("sched locks");
  if(t->t_state == T_RUNNING)
    panic("sched running");
  if(readeflags()&FL_IF)
    panic("sched interruptible");
  intena = mycpu()->intena;
  swtch(&t->context, mycpu()->scheduler);
  mycpu()->intena = intena;
}

// Give up the CPU for one scheduling round.
void
yield(void)
{
  acquire(&ptable.lock);  //DOC: yieldlock
  acquire(&myproc()->proclock);
  my_thread()->t_state = T_RUNNABLE;
  sched();
  release(&myproc()->proclock);
  release(&ptable.lock);
}

// A fork child's very first scheduling by scheduler()
// will swtch here.  "Return" to user space.
void
forkret(void)
{
  static int first = 1;
  // Still holding ptable.lock from scheduler.
  release(&myproc()->proclock);
  release(&ptable.lock);

  if (first) {
    // Some initialization functions must be run in the context
    // of a regular process (e.g., they call sleep), and thus cannot
    // be run from main().
    first = 0;
    iinit(ROOTDEV);
    initlog(ROOTDEV);
  }

  // Return to "caller", actually trapret (see allocproc).
}

// Atomically release lock and sleep on chan.
// Reacquires lock when awakened.
void
sleep(void *chan, struct spinlock *lk)
{
  struct proc *p = myproc();
  struct thread *t = my_thread();

  if(p == 0)
    panic("sleep");

  if(lk == 0)
    panic("sleep without lk");

  // Must acquire ptable.lock in order to
  // change p->state and then call sched.
  // Once we hold ptable.lock, we can be
  // guaranteed that we won't miss any wakeup
  // (wakeup runs with ptable.lock locked),
  // so it's okay to release lk.
  if (holding(&p->proclock))
      panic("holding proclock\n");
  if(lk != &ptable.lock){//} && lk != &p->proclock && (!holding(&p->proclock))){  //DOC: sleeplock0
    acquire(&ptable.lock);  //DOC: sleeplock1
    release(lk);
  }
//  if (holding(&p->proclock)){
//      release(&p->proclock);
//      if (lk != &p->proclock) release(lk);
//      acquire(&ptable.lock);
//  }
  acquire(&p->proclock);
  // Go to sleep.
  t->chan = chan;
  t->t_state= T_SLEEPING;

  sched();

  // Tidy up.
  t->chan = 0;
  // Reacquire original lock.
  release(&p->proclock);
  if(lk != &ptable.lock){  //DOC: sleeplock2
    release(&ptable.lock);
    acquire(lk);
  }
}

//PAGEBREAK!
// Wake up all processes sleeping on chan.
// The ptable lock must be held.
static void
wakeup1(void *chan) {
    struct proc *p;
    struct thread* thread;

    for (p = ptable.proc; p < &ptable.proc[NPROC]; p++) {
        if (p->state == USED) {
            acquire(&p->proclock);
            for (thread = p->threads; thread < &p->threads[NTHREADS]; thread++){
                if (thread->t_state == T_SLEEPING && thread->chan == chan)
                    thread->t_state = T_RUNNABLE;
            }
            release(&p->proclock);
        }
    }
}

// Wake up all processes sleeping on chan.
void
wakeup(void *chan)
{
  acquire(&ptable.lock);
  wakeup1(chan);
  release(&ptable.lock);
}

// Kill the process with the given pid.
// Process won't exit until it returns
// to user space (see trap in trap.c).
int
kill(int pid)
{
  struct proc *p;
  struct thread *t;

  acquire(&ptable.lock);
  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
    if(p->pid == pid){
      p->killed = 1;
      // Wake threads from sleep if necessary.
      acquire(&p->proclock);
      for (t = p->threads ; t < p->threads + NTHREADS; t++) {
          if (t->t_state == T_SLEEPING)
              t->t_state = T_RUNNABLE;
      }
      release(&p->proclock);
      release(&ptable.lock);
      return 0;
    }
  }
  release(&ptable.lock);
  return -1;
}

//PAGEBREAK: 36
// Print a process listing to console.  For debugging.
// Runs when user types ^P on console.
// No lock to avoid wedging a stuck machine further.

//TODO change procdump to give also thread information
void
procdump(void)
{
  static char *proc_states[] = {
  [UNUSED]    "unused",
  [EMBRYO]    "embryo",
  [ZOMBIE]    "zombie",
  [USED]      "used"
  };
  static char *thread_states[] = {
          [T_UNUSED] "T_UNUSED",
          [T_RUNNABLE] "T_RUNNABLE",
          [T_RUNNING] "T_RUNNING",
          [T_SLEEPING] "T_SLEEPING",

  };
  int i;
  struct proc *p;
  struct thread* thread;
  char *proc_state;
  uint pc[10];

  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
    if(p->state == UNUSED)
      continue;
  if(p->state >= 0 && p->state < NELEM(proc_states) && proc_states[p->state])
      proc_state = proc_states[p->state];
  else
      proc_state = "???";
  cprintf("PROCESS-\tpid: %d\tstate: %s\tname:%s\n", p->pid, proc_state, p->name);
      for (thread = p->threads; thread < &p->threads[NTHREADS]; thread++){
          if(thread->t_state == T_RUNNABLE || thread->t_state == T_RUNNING || thread->t_state == T_SLEEPING){
              getcallerpcs((uint*)thread->context->ebp+2, pc);
              cprintf("\tTHREADS:\n\ttid: %d\tstate: %s\tcore dump:",thread->tid, thread_states[thread->t_state]);
              for(i=0; i<10 && pc[i] != 0; i++)
                  cprintf(" %p", pc[i]);
              cprintf("\n");
          }
      }
      cprintf("\n");
  }
}

int kthread_create(void (*start_func)(), void * user_stack) {
    struct proc * p = myproc();
    struct thread * curthread = my_thread();
    struct thread * t;
    acquire(&p->proclock);

    if ((t = alloc_thread(p))==0) {
        release(&p->proclock);
        return 0;}

    *t->tf = *curthread->tf;
    t->tf->eip = (uint)start_func;
    t->tf->esp = (uint)user_stack;

    t->t_state = T_RUNNABLE;

    release(&p->proclock);

    return t->tid;
}

int kthread_join(int thread_id){
    struct proc * currproc = myproc();
    struct thread* thread;
    acquire(&currproc->proclock);
    for (thread = currproc->threads; thread < &currproc->threads[NTHREADS]; thread++){
        if(thread->tid == thread_id){
            if (thread->t_state == T_TERMINATED || thread->t_state == T_UNUSED){
                kfree(thread->kstack);
                thread->kstack = 0;
                thread->tid = 0;
                thread->t_state = T_UNUSED;
                release(&currproc->proclock);
                return 0;
            }
            while (thread->t_state != T_TERMINATED){
                sleep(currproc, &currproc->proclock);
            }
            kfree(thread->kstack);
            thread->kstack = 0;
            thread->tid = 0;
            thread->t_state = T_UNUSED;
            release(&currproc->proclock);
            return 0;
        }
    }
    release(&currproc->proclock);
    return -1;
}

int kthread_id(){
    return my_thread()->tid;
}

void kthread_exit(){
    //struct proc *curproc = myproc();
    struct thread * curr_thread = my_thread();
    struct proc* proc = myproc();
    acquire(&proc->proclock);
    curr_thread->killed = 1;
    if (is_last_thread()) {
        release(&proc->proclock);
        exit();
    }else {
        curr_thread->t_state = T_TERMINATED;
        release(&proc->proclock);
        wakeup(proc);
        acquire(&ptable.lock);
        acquire(&proc->proclock);
        sched();
        panic("zombie kthread_exit");
    }
}

//proclock must be held
int is_last_thread() {
    struct thread *curr_thread = my_thread();
    struct proc *proc = myproc();
    struct thread *t;
    if (!holding(&proc->proclock))
        panic(("proclock must be held before calling is_last_thread"));
    int last_thread = 1;
    for (t = proc->threads; t < &proc->threads[NTHREADS]; t++) {
        if ((t->t_state == T_RUNNABLE || t->t_state == T_RUNNING || t->t_state == T_SLEEPING) &&
            t->tid != curr_thread->tid) {
            last_thread = 0;
            break;
        }

    }
    return last_thread;
}

// should be called when mtable mutex is held
pthread_mutex_t * find_mutex(int mutex_id){
    if (!holding(&mtable.lock))
        panic("must hold mtable lock");
    pthread_mutex_t* mutex;
    for(mutex = mtable.mutexes ; mutex < &mtable.mutexes[MAX_MUTEXES];mutex++)
        if (mutex->id == mutex_id)
            return mutex;
    return 0;
}

int kthread_mutex_alloc(){
    acquire(&mtable.lock);
    pthread_mutex_t* mutex;
    for(mutex = mtable.mutexes ; mutex < &mtable.mutexes[MAX_MUTEXES];mutex++)
        if (mutex->m_state == M_UNUSED)
            goto found;

    release(&mtable.lock);
    return -1;


    found:
    mutex->m_state = M_USED;
    mutex->id = nextmid++;

// initializing mutexlock
    initsleeplock(&mutex->lock, "mutex lock");
    release(&mtable.lock);
    return mutex->id;
}

int kthread_mutex_dealloc(int mutex_id){
    pthread_mutex_t * mutex;
    acquire(&mtable.lock);
    mutex = find_mutex(mutex_id);
    if ((mutex == 0) || mutex->m_state == M_UNUSED) {
        release(&mtable.lock);
        return -1;
    }
    if(mutex->lock.locked){
        release(&mtable.lock);
        return -1;
    }
    mutex->m_state = M_UNUSED;
    mutex->id = 0;
    release(&mtable.lock);
    return 0;
}

int kthread_mutex_lock(int mutex_id){
    pthread_mutex_t * mutex;
    acquire(&mtable.lock);
    mutex = find_mutex(mutex_id);
    release(&mtable.lock);
    if ((mutex == 0)) {
        return -1;
    }
    acquiresleep(&mutex->lock);
    return 0;
}

int kthread_mutex_unlock(int mutex_id){
    pthread_mutex_t * mutex;
    acquire(&mtable.lock);
    mutex = find_mutex(mutex_id);
    release(&mtable.lock);
    if ((mutex == 0) || (!holdingsleep(&mutex->lock))) {
        return -1;
    }
    releasesleep(&mutex->lock);
    return 0;
}