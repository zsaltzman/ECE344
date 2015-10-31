#include <assert.h>
#include <stdlib.h>
#include <ucontext.h>
#include "thread.h"
#include "interrupt.h"

#define BYTE_ALIGNMENT 16

enum { READY = 0,
       RUNNING = 1,
       EXITED = 2
};

/* This is the thread control block */
struct thread {
  Tid thread_id;
  int state; //takes a value either READY, RUNNING, or EXITED
  ucontext_t context;
  void *stack_malloc_address; //used to keep track of the bottom of the memory we allocated for ths stack
  int pc_yield; //the number of times we've yielded. used to ensure we aren't stuck in a setcontext loop
  int kernel_thread_first_running; //used to ensure the kernel thread is enqueued properly
  int should_exit; //if 1, thread should exit itself as soon as it is run.
  int holding_lock;
};

//defines the structure of a queue node
typedef struct qn{
  struct qn *next;
  struct thread *node_t;
}queue_node;

//defines the structure of a thread queue (ready or exit).
typedef struct q{
  queue_node *head;
}thread_queue;

/* This is the wait queue structure */
struct wait_queue {
  thread_queue *waiting_queue;
};


struct wait_queue *
wait_queue_create()
{
	struct wait_queue *wq;

	wq = malloc(sizeof(struct wait_queue));
	assert(wq);

        wq->waiting_queue = (thread_queue *)malloc(sizeof(thread_queue));

	return wq;
}

//NOTE: does not delete the thread contained in qn for functionality reasons
void delete_node(queue_node *q_node)
{
  free(q_node);
}

//deletes the whole node, including the thread
void delete_node_and_thread(queue_node *q_node)
{
  free(q_node->node_t);
  free(q_node);
}

//deletes everything after the qn, including the thread queue itself.
//to delete everything, pass this function the head of a list.
void delete_all_in_queue(queue_node *q_node)
{
  if(q_node->next != NULL)
    delete_all_in_queue(q_node->next);
  free(q_node->node_t);
  free(q_node);
}

void
wait_queue_destroy(struct wait_queue *wq)
{

    assert(wq->waiting_queue->head==NULL);
    free(wq->waiting_queue);
    free(wq);
}

//adds a thread to the back of a queue
void enqueue(thread_queue *tq, queue_node *qn)
{
  queue_node *index; //used to navigate the thread_queue
  if (tq->head == NULL)
    tq->head = qn;
  else
    {
      index = tq->head;
      while(index->next != NULL)
	index = index->next;
      index->next = qn;
    }
  qn->next = NULL;
}

struct thread *find_node(thread_queue *tq, Tid t_id)
{
  queue_node *index = tq->head;
  while (index != NULL)
    {
      if (index->node_t->thread_id == t_id)
	return index->node_t;
      index = index->next;
    }
  return NULL;
}
//takes a thread off the head of the list and deletes the associated queue node.
//note that the thread is not destroyed by the deletion of the queue node.
struct thread *dequeue_head(thread_queue *tq)
{
  struct thread *t_ret = NULL;
  queue_node *q_delete = NULL;
  if (tq->head != NULL)
  {
    q_delete = tq->head;
    t_ret = tq->head->node_t;
    tq->head = tq->head->next;
  }
  
  delete_node(q_delete);
  return t_ret;
}

//dequeues a thread by id, returns NULL if the thread isn't in the queue.
//follows same rules as dequeue_head.
struct thread *dequeue_id(thread_queue *tq,Tid t_id)
{
  //prev is the node before index.
  queue_node *index = tq->head;
  queue_node *prev = NULL;
  
  struct thread *t_ret = NULL;
  
  //navigates until either t_ret is null or we find the thread we're looking for.
  while (index != NULL && index->node_t->thread_id != t_id)
  {
    prev = index;
    index = index->next;
  }
  
  if(index == NULL) //thread isn't there
    {
      t_ret = NULL;
    }
  else if(index == tq->head) //thread is first entry in queue
    {
      t_ret = tq->head->node_t;
      /*printf("removing node %i and setting head ",tq->head->node_t->thread_id);
      if(tq->head->next != NULL)
      printf("to node with id %i\n",tq->head->next->node_t->thread_id);
      else
      printf("\n");*/
      tq->head = tq->head->next;
    } 
  else if(index->next == NULL) //thread is at back of list
    {
      prev->next = NULL;
      t_ret = index->node_t;
    }
  else //all other cases
    {
      prev->next = index->next;
      t_ret = index->node_t;
    }
   delete_node(index);
  return t_ret;
}

void print_queue(thread_queue *tq)
{
  queue_node *qn = tq->head;
  printf("elements of the queue: \n");
  while(qn != NULL)
    {
    printf("%i  ",(int)(qn->node_t->thread_id));
    qn = qn->next;
    }
  printf("\n");
}

//GLOBAL DATA STRUCTURES:
//two queues to keep track of which threads are ready and which threads have exited.
//one thread to keep track of what's running.
//one array to keep track of which thread IDs are taken(0 is empty, 1 is taken).

thread_queue *ready_queue;
thread_queue *exit_queue;
struct wait_queue *thread_wait_queue;
struct wait_queue *lock_wait_queue;
struct thread *running_thread;
int tid_list[THREAD_MAX_THREADS];

void
thread_init(void)
{
  //set up the queues we're going to use
  ready_queue = (thread_queue *)malloc(sizeof(thread_queue));
  exit_queue = (thread_queue *)malloc(sizeof(thread_queue));
  thread_wait_queue = wait_queue_create();
  lock_wait_queue = wait_queue_create();
  
  //set up the tid_list
  int i = 0;
  while(i<THREAD_MAX_THREADS)
  {
   tid_list[i] = 0;
   i++;
  }


  //set up the thread we're running on right now, i.e. the one supplied by the OS
  struct thread *kernel_thread = (struct thread *)malloc(sizeof(struct thread));
  
  //since we can't copy the current context into our kernel thread and update it at the same time,
  //we'll need to handle this case explicitly.
  kernel_thread->kernel_thread_first_running = 1;
  
  kernel_thread->holding_lock = 0;
  
  //defined to be first thread
  kernel_thread->thread_id = 0;
  kernel_thread->pc_yield = 0;
  tid_list[0] = 1;
  
  //since the kernel thread is currently being executed
  kernel_thread->state = RUNNING;
  running_thread = kernel_thread;
}

//used to ensure proper thread exiting behavior is followed, i.e. a thread exits implicitly
//once it has finished running thread_main.
void thread_stub(void (*thread_main)(void *), void *arg)
{
  Tid ret;
  if (running_thread->should_exit)
    thread_exit(THREAD_SELF);
  //clear out exit queue, since this thread is guaranteed to not be exiting.
  //note that dequeue_head implicitly frees the node it pops.
  //note that all threads start with interrupts low.
  struct thread *index_thread = dequeue_head(exit_queue);
  while(index_thread != NULL)
  {
    free(index_thread->stack_malloc_address); //the original, pre-alignment address given to us by malloc
    free(index_thread);
    index_thread = dequeue_head(exit_queue);
    interrupts_set(1);
  }
  interrupts_set(1); //interrupts are low when the thread is created.
  thread_main(arg);
  ret = thread_exit(THREAD_SELF);
  //print_queue(ready_queue);
  assert(ret == THREAD_NONE);
  exit(0);
}

Tid
thread_id()
{
  return running_thread->thread_id;
}


Tid
thread_create(void (*fn) (void *), void *parg)
{
  interrupts_set(0);
  struct thread *t = (struct thread *)malloc(sizeof(struct thread));
  if(t == NULL)
    {
      printf("our threads have no memory\n");
      interrupts_set(1);
      return THREAD_NOMEMORY;
    }
  
  //find a thread id we can use and update that spot in the thread list.
  int i = 1; //NOTE: the zeroeth spot will always be taken as long as this program is running.
  while(i<THREAD_MAX_THREADS && tid_list[i] == 1)
    i++;
  if(i == THREAD_MAX_THREADS) //we are at capacity for threads.
    {
      interrupts_set(1);
      return THREAD_NOMORE;
    }
  else
    {
      t->thread_id = i;
      tid_list[i] = 1;
    }
  
  //copy the current context into the thread's context
  //note that we will be changing our thread's context to reflect the content of the new thread
  getcontext(&t->context);
  
  //update registers in our context:
  //set instruction pointer (RIP) to the start of thread_stub
  //set first argument register (RDI) to the address of the function we want to pass
  //set second argument register (RSI) to the address of our argument (whatever it is)
  
  t->context.uc_mcontext.gregs[REG_RIP] =(long long int) &thread_stub;
  t->context.uc_mcontext.gregs[REG_RDI] =(long long int) fn;
  t->context.uc_mcontext.gregs[REG_RSI] =(long long int) parg;
  
  t->kernel_thread_first_running = 0;
  t->pc_yield = 0;
  //allocate the stack
  void *stack_pointer;
  stack_pointer = malloc(THREAD_MIN_STACK); 
  if(stack_pointer == NULL)
    {
      free(t); //if our stack is invalid then this thread should be freed
      interrupts_set(1);
      return THREAD_NOMEMORY;
    }
  
  
  t->context.uc_stack.ss_sp = stack_pointer; //malloc gives you the top of the stack, which we want ss_sp to be;
  
  
  //set the stack pointer to be the top of our dynamically allocated stack.
  t->context.uc_mcontext.gregs[REG_RSP] = (long long int) stack_pointer+THREAD_MIN_STACK-8; //align to 16 bytes
  t->context.uc_stack.ss_size = THREAD_MIN_STACK-8;

  //when we free the stack, we'll need the address of the bottom of the memory addresses we allocated.
  t->stack_malloc_address = stack_pointer;
  t->kernel_thread_first_running = 0;
  t->should_exit = 0;
  t->holding_lock = 0;
  
  //put the thread in our ready queue
  queue_node *qn = (queue_node *)malloc(sizeof(queue_node));
  qn->node_t = t;
  enqueue(ready_queue, qn);
  interrupts_on();
  return t->thread_id;  
}

Tid
thread_yield(Tid want_tid)
{
  interrupts_off();
  Tid ret; 
  //check if we've got a valid thread for the Tid we're given
  //note that we should make sure our tid is positive, otherwise our enum values for threads won't work.
  if(want_tid >= -2 && want_tid < THREAD_MAX_THREADS )
    {     
      if (want_tid == THREAD_ANY && ready_queue->head== NULL) //no threads available to run
	{
	  interrupts_set(1); //so we aren't spinning in one thread.
	  // printf("no threads to run, carry on\n");
	  return THREAD_NONE;
	}
      if (want_tid >= 0 && tid_list[want_tid] == 0)
	{
	  interrupts_set(1);
	  return THREAD_INVALID;
	}
    }
  else
    {
      interrupts_set(1);
      return THREAD_INVALID;
    }
  
  
  //if we get to this point we know we have a valid thread to run.
  //place the running thread back in the ready queue and save its context here.
  queue_node *qn = (queue_node *)malloc(sizeof(queue_node));
  qn->node_t = running_thread;
  enqueue(ready_queue,qn);
  Tid oldval = running_thread->thread_id;
  getcontext(&qn->node_t->context);
  if(running_thread->pc_yield == 0)
   {
     running_thread->pc_yield = 1; //make sure the thread that is yielding, i.e. the one that is currently
                                   //running doesn't come in here twice.
     //if any thread is acceptable to run, pop the head of the queue and run it.
     if(want_tid == THREAD_ANY)
       running_thread = dequeue_head(ready_queue);
     else if(want_tid == THREAD_SELF)
       running_thread = dequeue_id(ready_queue,qn->node_t->thread_id);
     else
       running_thread = dequeue_id(ready_queue, want_tid);
   
     ret = running_thread->thread_id;
     printf("thread %d yielding to %d\n",oldval,ret);
     setcontext(&running_thread->context);
   }
  
  //if we're marked for exit, exit.
  if (running_thread->should_exit)
    thread_exit(THREAD_SELF);
  
  //we also clear the exit queue here to ensure that exited threads are cleaned up whenever another thread runs
  struct thread *index_thread = dequeue_head(exit_queue);
  
  while(index_thread != NULL)
  {
    free(index_thread->stack_malloc_address); //the original, pre-alignment address given to us by malloc
    free(index_thread);
    index_thread = dequeue_head(exit_queue);
  }

  //we'll eventually come back to this point once our thread runs again, so return the value
  //of the thread that took over.
  //we also reset the flag of the thread that had previously yielded to make sure that we can yield again.
  running_thread->pc_yield = 0;
  interrupts_on();
  return ret;
}


//Note that this function is the same as thread_yield, except the yielding thread does not put itself in the ready queue.
//Used to ensure that thread_exit(THREAD_SELF) works properly. 
Tid
thread_yield_from_exit(Tid want_tid)
{
  interrupts_set(0); //will get set high by the next running thread.
  queue_node *qn = (queue_node *)malloc(sizeof(queue_node));
  qn->node_t = running_thread;
  tid_list[qn->node_t->thread_id] = 0;
  enqueue(exit_queue,qn);
  running_thread = dequeue_head(ready_queue);
  // printf("running thread %d\n",running_thread->thread_id);
  setcontext(&running_thread->context);

  //This thread should be getting exited, so we should never come back here.
  running_thread->pc_yield = 0;
  return THREAD_FAILED;
}

Tid
thread_yield_from_wait(thread_queue *tq, Tid want_tid)
{
  interrupts_off();
  Tid ret; 
  //we've already performed our error checks, so no need to do it here.
  //note that we are always calling this with THREAD_ANY
  
  //if we get to this point we know we have a valid thread to run.
  //place the running thread back in the ready queue and save its context here.
  queue_node *qn = (queue_node *)malloc(sizeof(queue_node));
  qn->node_t = running_thread;
  //Tid oldthread = running_thread->thread_id;
  enqueue(tq,qn);
  getcontext(&qn->node_t->context);
  if(running_thread->pc_yield == 0)
   {
     running_thread->pc_yield = 1; //make sure the thread that is yielding, i.e. the one that is currently
                                   //running doesn't come in here twice.
     //if any thread is acceptable to run, pop the head of the queue and run it.
     running_thread = dequeue_head(ready_queue);
   
     ret = running_thread->thread_id;
     // printf("thread %d yielding to thread %d\n",oldthread,running_thread->thread_id);
     setcontext(&running_thread->context);
   }
  
  //if we're marked for exit, exit.
  if (running_thread->should_exit)
    thread_exit(THREAD_SELF);
  
  //we also clear the exit queue here to ensure that exited threads are cleaned up whenever another thread runs
  struct thread *index_thread = dequeue_head(exit_queue);
  
  while(index_thread != NULL)
  {
    free(index_thread->stack_malloc_address); //the original, pre-alignment address given to us by malloc
    free(index_thread);
    index_thread = dequeue_head(exit_queue);
  }

  //we'll eventually come back to this point once our thread runs again, so return the value
  //of the thread that took over.
  //we also reset the flag of the thread that had previously yielded to make sure that we can yield again.
  running_thread->pc_yield = 0;
  interrupts_on();
  return ret;
}

Tid
thread_exit(Tid tid)
{
  interrupts_set(0); //accessing shared structures, have to disable interrupts.
  //if we don't have any more threads to run
  if((tid == THREAD_ANY || tid == THREAD_SELF) && ready_queue->head == NULL)
    return THREAD_NONE;
  struct thread *exiting_thread = NULL;

  //if the thread isn't in our tid_list
  if(tid < -2 || tid >= THREAD_MAX_THREADS || (tid > 0 && tid_list[tid] == 0))    
      return THREAD_INVALID;
  //if we reach this point we know we have a valid thread
  interrupts_set(1);
  

  if(tid == THREAD_ANY)
    {
      interrupts_set(0);
      queue_node *qn = ready_queue->head;
      while (qn != NULL && qn->node_t->should_exit == 1)
	qn = qn->next;
      qn->node_t->should_exit = 1;
      exiting_thread = qn->node_t;
    }
  else if(tid == THREAD_SELF) //in this case we must yield to another thread
    {
      exiting_thread = running_thread;
      thread_yield_from_exit(THREAD_ANY); //yield to the next thread in the list.
      assert(0);
    }
  else
    {
      interrupts_set(0);
      exiting_thread = find_node(ready_queue,tid);
      if (exiting_thread == NULL)
	exiting_thread = find_node(lock_wait_queue->waiting_queue,tid);
      if (exiting_thread == NULL)
	exiting_thread = find_node(thread_wait_queue->waiting_queue,tid);
      exiting_thread->should_exit = 1;
    }
    interrupts_set(1);
    // printf("thread %d marked for exit\n",exiting_thread->thread_id);
    return exiting_thread->thread_id;
}

/*******************************************************************
 * Important: The rest of the code should be implemented in Lab 3. *
 *******************************************************************/




Tid
thread_sleep(struct wait_queue *queue)
{
  //initial checks to see if we can successfully perform a wait
  interrupts_off();
  if (queue == NULL)
    {
      interrupts_on();
      return THREAD_INVALID;
    }
  if(ready_queue->head == NULL)
    {
      interrupts_on();
      return THREAD_NONE;
    }
  return thread_yield_from_wait(queue->waiting_queue,THREAD_ANY);
}

/* when the 'all' parameter is 1, wakeup all threads waiting in the queue.
 * returns whether a thread was woken up on not. */
int
thread_wakeup(struct wait_queue *queue, int all)
{
  interrupts_off(); //wait queues must be accessed atomically.
  queue_node *qn = (queue_node *)malloc(sizeof(queue_node));
  int count = 1;
  if(queue == NULL || queue->waiting_queue->head == NULL)
    {
      interrupts_on();
      return 0;
    }
  else
    {
      if(!all)
	{
	  qn->node_t = dequeue_head(queue->waiting_queue);
	  enqueue(ready_queue,qn);
	}
      else
	{
	  // printf("wait queue before wakeup.\n");
	  // print_queue(queue->waiting_queue);
	  count = 0;
	  qn->node_t = dequeue_head(queue->waiting_queue);
	  while(qn->node_t != NULL)
	    {
	      enqueue(ready_queue,qn);
	      count++;
	      qn = (queue_node *)malloc(sizeof(queue_node));
	      qn->node_t = dequeue_head(queue->waiting_queue);
	    }
	  // printf("ready queue after wakeup\n");
	  // print_queue(ready_queue);
	}
    }
  interrupts_on();
  return count;
}

struct lock {
  struct wait_queue *lock_queue;
  int locked;
};

struct lock *
lock_create()
{
	struct lock *lock;

	lock = malloc(sizeof(struct lock));
	assert(lock);
	
        lock->lock_queue = wait_queue_create();
	lock->locked = 0;

	return lock;
}

void
lock_destroy(struct lock *lock)
{
        interrupts_off(); //we're checking shared structures, so we have to lock.
        assert(lock != NULL);
	if(!lock->locked)
        {
	  assert(lock->lock_queue->waiting_queue->head == NULL);
	  wait_queue_destroy(lock->lock_queue);
	  free(lock);
	}
	interrupts_on();
}

void
lock_acquire(struct lock *lock)
{
	interrupts_off(); //interrupts have to be off while checking the lock, obviously.
	assert(lock != NULL);
	while(lock->locked)
	  thread_sleep(lock->lock_queue);
	printf("thread %d acquired lock.\n",running_thread->thread_id);
	lock->locked = 1;
	running_thread->holding_lock = 1;
	interrupts_on(); //we should allow other threads to interrupt us after this point.
}

void
lock_release(struct lock *lock)
{
	assert(lock != NULL);
	interrupts_off();
	if(lock->locked && running_thread->holding_lock)
	  {
	    // printf("thread %d releasing the lock\n",running_thread->thread_id);
	    lock->locked = 0;
	    running_thread->holding_lock = 0;
	    thread_wakeup(lock->lock_queue,1);
	  }
	interrupts_on();
}

struct cv {
  struct wait_queue *cv_queue;
};

struct cv *
cv_create()
{
	struct cv *cv;

	cv = malloc(sizeof(struct cv));
	assert(cv);
	cv->cv_queue = wait_queue_create();

	return cv;
}

void
cv_destroy(struct cv *cv)
{
	assert(cv != NULL);
	assert(cv->cv_queue->waiting_queue->head == NULL);
	interrupts_off();
	wait_queue_destroy(cv->cv_queue);
	free(cv);
	interrupts_on();
}

void
cv_wait(struct cv *cv, struct lock *lock)
{
	assert(cv != NULL);
	assert(lock != NULL);
	if(running_thread->holding_lock)
	  {
	    interrupts_off();
	    lock_release(lock);
	    printf("thread %d releasing lock and yielding\n",running_thread->thread_id);
	    thread_yield_from_wait(cv->cv_queue->waiting_queue,THREAD_ANY);
	    interrupts_on();
	  }
}

void
cv_signal(struct cv *cv, struct lock *lock)
{
	assert(cv != NULL);
	assert(lock != NULL);
	if(running_thread->holding_lock)
	  thread_wakeup(cv->cv_queue,0);
	else
	  printf("thread %d attempted to signal without having the lock.\n",running_thread->thread_id);
}

void
cv_broadcast(struct cv *cv, struct lock *lock)
{
	assert(cv != NULL);
	assert(lock != NULL);
	if(running_thread->holding_lock)
	  thread_wakeup(cv->cv_queue,1);
}
