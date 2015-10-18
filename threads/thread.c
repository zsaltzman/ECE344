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
struct thread *running_thread;
int tid_list[THREAD_MAX_THREADS];

void
thread_init(void)
{
  //set up the queues we're going to use
  ready_queue = (thread_queue *)malloc(sizeof(thread_queue));
  exit_queue = (thread_queue *)malloc(sizeof(thread_queue));

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
  
  //clear out exit queue, since this thread is guaranteed to not be exiting.
  //note that dequeue_head implicitly frees the node it pops.
  struct thread *index_thread = dequeue_head(exit_queue);
  while(index_thread != NULL)
  {
    free(index_thread->stack_malloc_address); //the original, pre-alignment address given to us by malloc
    free(index_thread);
    index_thread = dequeue_head(exit_queue);
  }
  
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

//NOTE: problem is coming from queue not being set properly (skipping ahead nodes for some reason)
Tid
thread_create(void (*fn) (void *), void *parg)
{
  struct thread *t = (struct thread *)malloc(sizeof(struct thread));
  if(t == NULL)
    {
      printf("our threads have no memory\n");
    return THREAD_NOMEMORY;
    }
  //find a thread id we can use and update that spot in the thread list.
  int i = 1; //NOTE: the zeroeth spot will always be taken as long as this program is running.
  while(i<THREAD_MAX_THREADS && tid_list[i] == 1)
    i++;
  if(i == THREAD_MAX_THREADS) //we are at capacity for threads.
    {
      // printf("we are at capacity for threads\n");
      return THREAD_NOMORE;
    }
  else
    {
      // printf("setting tid_list[%d] to 1\n",i);
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
      return THREAD_NOMEMORY;
    }
  
  //We have have to adjust the first pointer in order to be byte addressable to 16
  //Note that the stack grows downwards (lowest address is the top of the stack)
  long int alignment_factor_top = 0;
  long int alignment_factor_bot = 0;
  while ((long long int)(stack_pointer+alignment_factor_top) % 16 != 0)
    alignment_factor_top++;
  
  t->context.uc_stack.ss_sp = stack_pointer+alignment_factor_top; //malloc gives you the top of the stack, which we want ss_sp to be;
  
  
  while((long long int)((stack_pointer+THREAD_MIN_STACK-1)-alignment_factor_bot)%16 != 0)
    alignment_factor_bot++;

  //set the stack pointer to be the top of our dynamically allocated stack.
  t->context.uc_mcontext.gregs[REG_RSP] = (long long int) stack_pointer+THREAD_MIN_STACK-alignment_factor_bot;
  t->context.uc_stack.ss_size = THREAD_MIN_STACK-alignment_factor_top-alignment_factor_bot;

  //when we free the stack, we'll need the address of the bottom of the memory addresses we allocated.
  t->stack_malloc_address = stack_pointer;
  t->kernel_thread_first_running = 0;
  
  //put the thread in our ready queue
  queue_node *qn = (queue_node *)malloc(sizeof(queue_node));
  qn->node_t = t;
  enqueue(ready_queue, qn);
  return t->thread_id;  
}

Tid
thread_yield(Tid want_tid)
{
  Tid ret; 
  //check if we've got a valid thread for the Tid we're given
  //note that we should make sure our tid is positive, otherwise our enum values for threads won't work.
  if(want_tid >= -2 && want_tid < THREAD_MAX_THREADS )
    {     
      if (want_tid == THREAD_ANY && ready_queue->head== NULL) //no threads available to run
        return THREAD_NONE;
      if (want_tid >= 0 && tid_list[want_tid] == 0)
        return THREAD_INVALID;
    }
  else
    return THREAD_INVALID;
  
  
  //if we get to this point we know we have a valid thread to run.
  //place the running thread back in the ready queue and save its context here.
  queue_node *qn = (queue_node *)malloc(sizeof(queue_node));
  qn->node_t = running_thread;
  enqueue(ready_queue,qn);
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
     setcontext(&running_thread->context);
   }

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
  return ret;
}


//Note that this function is the same as thread_yield, except the yielding thread does not put itself in the ready queue.
//Used to ensure that thread_exit(THREAD_SELF) works properly. 
Tid
thread_yield_from_exit(Tid want_tid)
{
  queue_node *qn = (queue_node *)malloc(sizeof(queue_node));
  qn->node_t = running_thread;
  tid_list[qn->node_t->thread_id] = 0;
  enqueue(exit_queue,qn);
  
  if(running_thread->pc_yield == 0)
   {
     running_thread->pc_yield = 1; //make sure the thread that is yielding, i.e. the one that is currently
                                   //running doesn't come in here twice.
    
     running_thread = dequeue_head(ready_queue);
     
     setcontext(&running_thread->context);
   }

  //This thread should be getting exited, so we should never come back here.
  running_thread->pc_yield = 0;
  return THREAD_FAILED;
}

Tid
thread_exit(Tid tid)
{
  //if we don't have any more threads to run
  if((tid == THREAD_ANY || tid == THREAD_SELF) && ready_queue->head == NULL)
    return THREAD_NONE;
   

  //if the thread isn't in our tid_list
  if(tid < -2 || tid >= THREAD_MAX_THREADS || (tid > 0 && tid_list[tid] == 0))
    {
      
      return THREAD_INVALID;
    }

  //if we reach this point we know we have a valid thread
  queue_node *qn = (queue_node *)malloc(sizeof(queue_node));
  

  if(tid == THREAD_ANY)
    {
      qn->node_t = dequeue_head(ready_queue);
      tid_list[qn->node_t->thread_id] = 0;
    }
  else if(tid == THREAD_SELF) //in this case we must yield to another thread
    {
      thread_yield_from_exit(THREAD_ANY); //yield to the next thread in the list.
    }
  else
    {
      qn->node_t = dequeue_id(ready_queue,tid);
      tid_list[tid] = 0;
    }
    enqueue(exit_queue,qn); //takes a thread out of the ready queue and puts it into the exit queue
    return qn->node_t->thread_id;
}

/*******************************************************************
 * Important: The rest of the code should be implemented in Lab 3. *
 *******************************************************************/

/* This is the wait queue structure */
struct wait_queue {
	/* ... Fill this in ... */
};

struct wait_queue *
wait_queue_create()
{
	struct wait_queue *wq;

	wq = malloc(sizeof(struct wait_queue));
	assert(wq);

	TBD();

	return wq;
}

void
wait_queue_destroy(struct wait_queue *wq)
{
	TBD();
	free(wq);
}

Tid
thread_sleep(struct wait_queue *queue)
{
	TBD();
	return THREAD_FAILED;
}

/* when the 'all' parameter is 1, wakeup all threads waiting in the queue.
 * returns whether a thread was woken up on not. */
int
thread_wakeup(struct wait_queue *queue, int all)
{
	TBD();
	return 0;
}

struct lock {
	/* ... Fill this in ... */
};

struct lock *
lock_create()
{
	struct lock *lock;

	lock = malloc(sizeof(struct lock));
	assert(lock);

	TBD();

	return lock;
}

void
lock_destroy(struct lock *lock)
{
	assert(lock != NULL);

	TBD();

	free(lock);
}

void
lock_acquire(struct lock *lock)
{
	assert(lock != NULL);

	TBD();
}

void
lock_release(struct lock *lock)
{
	assert(lock != NULL);

	TBD();
}

struct cv {
	/* ... Fill this in ... */
};

struct cv *
cv_create()
{
	struct cv *cv;

	cv = malloc(sizeof(struct cv));
	assert(cv);

	TBD();

	return cv;
}

void
cv_destroy(struct cv *cv)
{
	assert(cv != NULL);

	TBD();

	free(cv);
}

void
cv_wait(struct cv *cv, struct lock *lock)
{
	assert(cv != NULL);
	assert(lock != NULL);

	TBD();
}

void
cv_signal(struct cv *cv, struct lock *lock)
{
	assert(cv != NULL);
	assert(lock != NULL);

	TBD();
}

void
cv_broadcast(struct cv *cv, struct lock *lock)
{
	assert(cv != NULL);
	assert(lock != NULL);

	TBD();
}
