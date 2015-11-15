#include <assert.h>
#include <stdlib.h>
#include <ucontext.h>
#include "thread.h"
#include "interrupt.h"
#include <stdbool.h>

/* This is the thread control block */
struct thread {
	int yield; //0 if hasn't yielded, 1 if has
	int state; //indicate what state the thread is in
				/* 0 = put in ready queu
				   1 = put in exit queu
				   */
	Tid id; //create an index of thread ids for the scheduler
	struct thread *next; //create a linked list of threads
	ucontext_t context; //the context of the thread
	unsigned long diff; //the difference in memory between aligned value and allocated value
};


struct thread *exitqueu = NULL;
struct thread *queu = NULL;
struct thread *currentThread = NULL; // points to which thread is currently running
bool threadsMade[1024]; //keep track of thread id's

void thread_stub (void(*thread_main)(void*), void *arg);

void free_exit_queu();

void
thread_init(void)
{
	int i;
	for (i = 0; i<1024; i++)
	{
		threadsMade[i] = false;
	}
	threadsMade[0] = true; //we will assign this id soon
	struct thread *protothread; //first thread to be initialized
	protothread = (struct thread*)malloc(sizeof(struct thread)); //allocate memory for the thread
	protothread->id = 0;
	protothread->next = NULL;
	protothread->yield = 0; //hasn't yielded yet
	currentThread = protothread;
	return;
}

Tid
thread_id()
{
	return currentThread->id; //return the value of the initial thread
}

Tid
thread_create(void (*fn) (void *), void *parg)
{
	int enable = interrupts_set(0); //disable interrupts and return the value opf previous interrupt state
	int i = 0;
	for (i = 0; i < THREAD_MAX_THREADS+1; i++)
	{
		if (i == 1024) //if it has gone through 1023 iterations (1-1023) without finding vacant thread then max threads was reached
		{
			interrupts_set(enable);
			return THREAD_NOMORE; // No more threads may be created 
		}
		else if (threadsMade[i] == false)
		{
			threadsMade[i] = true;
			break; // i holds the value of the new thread now
		}


	}

	struct thread *newThread;
	newThread = (struct thread*)malloc(sizeof(struct thread));

	if (newThread == NULL){
		interrupts_set(enable);
		return THREAD_NOMEMORY;
	}
	getcontext (&newThread->context);

	(newThread->context).uc_mcontext.gregs[REG_RIP] = (unsigned long) &thread_stub; //point to the stub function immediately
	newThread->id = i;
	newThread->yield = 0; //initialize to hasn't yielded yet
	newThread->state = 0; //initialize state to not get ready for exit queu
	newThread->next = NULL;

	(newThread->context).uc_stack.ss_sp = malloc(THREAD_MIN_STACK + 16); //ss_sp now point to the lowest memory address
	if ((newThread->context).uc_stack.ss_sp == NULL){
		interrupts_set(enable);
		return THREAD_NOMEMORY;
	}
	newThread->diff = (long long int)(newThread->context).uc_stack.ss_sp % 16;
	(newThread->context).uc_stack.ss_sp += newThread->diff; //align to memory address divisible by 16
	(newThread->context).uc_mcontext.gregs[REG_RSP] = (unsigned long) ((newThread->context).uc_stack.ss_sp + THREAD_MIN_STACK - 8); //point to top of the stack
	(newThread->context).uc_mcontext.gregs[REG_RDI] = (unsigned long) fn;
	(newThread->context).uc_mcontext.gregs[REG_RSI] = (unsigned long) parg; //setup the arguments
	//now put the thread in the ready queu
	//printf ("New thread %d created\n", newThread->id);
	if (queu == NULL)
	{
		queu = newThread;
		interrupts_set(enable);
		return newThread->id;
	}
	struct thread *ptr = queu;

	while (ptr != NULL)
	{
		
		if (ptr->next == NULL)
		{
			ptr->next = newThread;
			newThread->next = NULL;
			interrupts_set(enable);
			return newThread->id;
		}
		ptr = ptr->next;
	}

	//return the thread's id
	interrupts_set(enable);
	return newThread->id;
}

Tid
thread_yield(Tid want_tid)
{
	int enable = interrupts_set(0);
	if (exitqueu != NULL) //keep checking if the exitqueu needs to be freed
		free_exit_queu();

	if (want_tid > THREAD_MAX_THREADS){
		interrupts_set(enable);
		return THREAD_INVALID;
	}
	if (want_tid == THREAD_SELF || want_tid == currentThread->id)
	{
		interrupts_set(enable);
		return currentThread->id;
	}
	if (want_tid == THREAD_ANY) //Implement this
	{
		assert (interrupts_enabled () == 0); 
		if(queu == NULL){
			assert (interrupts_enabled () == 0); 
			interrupts_set(enable);
			return THREAD_NONE; //if the initial thread is the only thread available
		}
		//else use the first thread in the ready queu
		struct thread *temp = queu;
		struct thread *ctemp = currentThread;

		getcontext (&currentThread->context);
		//printf ("The yield value of thread %d is %d\n", ctemp->id, ctemp->yield);
		//printf ("we get here?\n");
		if (ctemp->yield == 0)
		{
			//printf ("why here\n");
			ctemp->yield = 1; //for returning
			queu = temp->next; //pop first element out of queu
			currentThread = temp;
			temp->next = NULL;

			if (ctemp->state == 0) // if state = 0 place in ready queu
			{
				assert (interrupts_enabled () == 0); 
				if (queu == NULL){  //if queu is empty simply put current thread to front of queu
					assert (interrupts_enabled () == 0); 
					ctemp->next = NULL;
					queu = ctemp;
				}
				else{
					
					//printf ("queu not null and temp id is %d\n", temp->id);
					struct thread *ptr = queu;
					while (1)
					{
						if (ptr->next == NULL)
						{
							ptr->next = ctemp; //add the previous running thread to back of queu
							ctemp->next = NULL;
							break; //leave the loop
						}
						ptr = ptr->next;
					}
				}
			}
			else if (ctemp->state == 1) //else it's ready to exit, place in exit queu
			{
				ctemp->next = exitqueu;
				exitqueu = ctemp;
			}
			
			setcontext (&temp->context); //switch to new thread's context
		}
		ctemp->yield = 0; //reset yield value
		interrupts_set(enable);
		return temp->id;

	}

	//At this point we know it has called a specific thread id
	struct thread *temp = queu;
	struct thread *prevtemp = NULL; //stores value in the list before temp
	struct thread *ctemp = currentThread;
	while (temp != NULL)
	{
		if (temp->id == want_tid) //if the thread id corresponds to a ready thread
		{
				getcontext (&currentThread->context);
				//printf ("The yield value of thread %d is %d\n", ctemp->id, ctemp->yield);
				//printf ("is it specific\n");
				//printf ("on the thread we want\n");
				if (ctemp->yield == 0){
					//printf ("has a specific thread been checked\n");
					ctemp->yield = 1; 
					if (prevtemp == NULL){ //if the thread we want is first on the queu{
						queu = temp->next; //pop the first element out
						temp->next = NULL;
					}
					else{
						prevtemp->next = temp->next; //remove temp from the ready queu
						temp->next = NULL;
					}
					currentThread = temp; //now temp is the current thread running
					
					// now put active thread to the back of the queu
					if (queu == NULL){
						queu = ctemp;
						ctemp->next = NULL;
					}
					else {
						struct thread *ptr = queu;
						while (ptr != NULL)
						{
							if (ptr->next == NULL)
							{
								ptr->next = ctemp; //add the previous running thread to back of queu
								ctemp->next = NULL;
								break; //leave the loop
							}
							ptr = ptr->next;
						}
					}
					setcontext (&temp->context); //switch to this thread's context
				}
				//else we are returning now
				//printf ("restoring yield\n");
				ctemp->yield = 0; //reset this thread's yield flag
			interrupts_set(enable);
			return want_tid;
		}
		prevtemp = temp;
		temp = temp->next;
	}

	// at this point want_tid isn't in the queu
	interrupts_set(enable);
	return THREAD_INVALID;
}

Tid
thread_exit(Tid tid)
{

	int enable = interrupts_set (0);
	if (tid == THREAD_SELF || tid == currentThread->id) //if program wants to delete active thread
	{
		if (queu == NULL){
			interrupts_set(enable);
			return THREAD_NONE;
		}
		struct thread *ctemp = currentThread;
		ctemp->state = 1; //mark to be placed in the exit queu
		threadsMade[ctemp->id] = false; //free room for that thread id
		thread_yield(THREAD_ANY); //yield to next ready thread, place in exit queu
		interrupts_set(enable);
		return ctemp->id;
	}


	if (tid == THREAD_ANY) //delete any thread
	{
		if (queu == NULL){
			interrupts_set(enable);
			return THREAD_NONE;
		}
		struct thread *ptr = queu;
		queu = ptr->next; //pop first element out of queu
		Tid retid = ptr->id;
		if (ptr->id != 0)
		{
			threadsMade[ptr->id] = false; //free that id
			(ptr->context).uc_stack.ss_sp = (ptr->context).uc_stack.ss_sp - ptr->diff; //restore location of initial allocation
			free ((ptr->context).uc_stack.ss_sp);
		}
		free (ptr);
		interrupts_set(enable);
		return retid;
	}

	//At this point we check the ready queu for the thread in question
	struct thread *temp = queu;
	struct thread *prevtemp = NULL; //points behind the temp pointer

	while (temp != NULL)
	{

		if (temp->id == tid) //if we found the id we're looking for in the queu
		{
			if (prevtemp == NULL){ //If it's the first node in the queu
				queu = temp->next; //pop the first element out of queu
				temp->next = NULL;
			}

			else //else it's not the first element
			{
				prevtemp->next = temp->next; //pop temp out of the queu
				temp->next = NULL;
			}

			threadsMade[temp->id] = false; //free room for this thread id
			if (temp->id != 0)
			{
				(temp->context).uc_stack.ss_sp = (temp->context).uc_stack.ss_sp - temp->diff; //restore location of initial allocation
				free ((temp->context).uc_stack.ss_sp);
			}
			free (temp);
			interrupts_set(enable);
			return tid; //return id of the thread we just deleted
			
		}

		prevtemp = temp;
		temp = temp->next;
	}
	interrupts_set(enable);
	return THREAD_INVALID; //all possible threads have been checked and the id hasn't been found
}

void 
thread_stub (void(*thread_main)(void*), void *arg){
	int enable = interrupts_set(1);
	Tid ret;
	thread_main(arg);
	ret = thread_exit(THREAD_SELF);
	//only get here if thread 0 is the only thread leftover
	assert(ret == THREAD_NONE);
	interrupts_set(enable);	
	exit(0); // all threads destroyed, we can go home now
}
void
printQueu ()
{
	int enable = interrupts_set(0);
	printf ("The queu so far: \n");
	struct thread *temp = queu;
	while (temp != NULL)
	{
		printf ("%d\n", temp->id);
		temp = temp->next;
	}
	interrupts_set(enable);
	return;
}

void
free_exit_queu ()
{
	int enable = interrupts_set (0);
	struct thread *temp = exitqueu;

	while (exitqueu != NULL)
	{
		exitqueu = temp->next; //keep popping the first element out of the exit queu

		(temp->context).uc_stack.ss_sp = (temp->context).uc_stack.ss_sp - temp->diff; //restore location of initial allocation
		free ((temp->context).uc_stack.ss_sp);
		free (temp);

		temp = exitqueu; 
	}
	interrupts_set(enable);
	return;
}

/* This is the wait queue structure */
struct wait_queue {
	struct thread *next; //points to first thread in the wait queue
	unsigned long numb; //number of threads in the queue at any given time
};

struct wait_queue *
wait_queue_create()
{
	int enable = interrupts_set(0);
	struct wait_queue *wq;

	wq = malloc(sizeof(struct wait_queue));
	assert(wq);

	wq->next = NULL; //initialize the wait queue to point to NULL
	wq->numb = 0; //no threads in the queue at the beginning
	interrupts_set(enable);
	return wq;
}

void
wait_queue_destroy(struct wait_queue *wq)
{
	int enable = interrupts_set(0);	
	struct thread *temp = wq->next; //create temp point to threads in the queue

	if (temp != NULL) //if there are actually threads in the wait queue
	{
		while (wq->next != NULL)
		{
			wq->next = temp->next;
			(temp->context).uc_stack.ss_sp = (temp->context).uc_stack.ss_sp - temp->diff; //restore location of initial allocation
			free ((temp->context).uc_stack.ss_sp); //free the stack
			free (temp);
		}
	}
	free(wq); //finally free the wait queue (the head of the list)
	interrupts_set(enable);
	return;
}

Tid
thread_sleep(struct wait_queue *queue)
{
	int enable = interrupts_set(0);
	int retid;
	if (queue == NULL) //if no threads are in the wait queu
	{
		interrupts_set(enable);	
		return THREAD_INVALID;
	}
	if (queu == NULL){ //If no threads are in the ready queu
		interrupts_set(enable);	
		return THREAD_NONE;
	}
	//at this point the queue has at least one element in it and is valid
	struct thread *ctemp = currentThread; //pointer to the current active thread
	struct thread *ptr = queue->next; // create a thread pointer to traverse ready queue
	struct thread *temp = queu; //first value in ready queu
	
	getcontext (&ctemp->context);
	if (ctemp->state == 0)
	{
		currentThread->state = 1;
		//now place current thread in wait queue
		if (ptr == NULL)
		{
			queue->next = ctemp; //current active thread becomes first thread in the wait queue
			ctemp->next = NULL;
			queue->numb++; //increment number of threads in the queue counter
		}
		else
		{
			while (1) 
			{
				if (ptr->next == NULL) //put current thread to end of the wait queue
				{
					ptr->next = ctemp;
					ctemp->next = NULL; //just for good measure
					queue->numb++;
					break;
				}
				ptr = ptr->next; //increment pointer
			}
		}
	
		queu = queu->next; //pop out next thread in ready queu
		temp->next = NULL;
		retid = temp->id;  
		currentThread = temp; //current thread is now the next thread in the ready queu
		setcontext(&temp->context); //context switch
	}
	ctemp->state = 0; //reset it's state variable
	interrupts_set(enable);	
	return retid;
}

/* when the 'all' parameter is 1, wakeup all threads waiting in the queue.
 * returns whether a thread was woken up on not. */
int
thread_wakeup(struct wait_queue *queue, int all)
{
	int enable = interrupts_set(0);
	if (queue == NULL || queue->next == NULL) //if the queue is invalid or has no threads in it
		{
			interrupts_set(enable);	
			return 0;
		}
	//at this point we know that the wait queue is valid and has at least one thread in it
	if (all == 0){ //if only want to remove single thread from wait queue
		struct thread *temp = queue->next;
		struct thread *ptr = queu;
		queue->next = temp->next; //pop the thread out of the wait queue
		temp->next = NULL;
		//printQueue(queue);
		if (ptr == NULL)
		{
			queu = temp; //put temp at the beginning of ready queu
			queue->numb--; //subtract number of threads in the queue
			//printQueu();
			interrupts_set(enable);	
			return 1; //return 1 thread woken up
		}

		//else there are more than one thread in the queu
		while (1)
		{
			if (ptr->next == NULL)
			{
				ptr->next = temp; //insert thread to end of the ready queu
				queue->numb--;
				//printQueu();
				interrupts_set(enable);	
				return 1;
			}
			ptr = ptr->next; //increment pointer
		}
	}

	else if (all == 1) //if we want to empty the entire wait queue
	{
		unsigned long ret_number_of_threads = queue->numb;
		while (queue->next != NULL){ //while the queue isn't empty
			struct thread *temp = queue->next;
			struct thread *ptr = queu;
			queue->next = temp->next; //pop value out of wait queue
			temp->next = NULL;

			if (ptr == NULL) //put the thread to the head of the run queu
			{
				queu = temp;
				queue->numb--;
			}
			else if (ptr != NULL)
			{
				while (1) //put the thread to the end of the run queu
				{
					if (ptr->next == NULL)
					{
						ptr->next = temp;
						queue->numb--;
						break;
					}
					ptr = ptr->next; //increment pointer
				}
			}
		}
		interrupts_set(enable);	
		return ret_number_of_threads;
	}
	interrupts_set(enable);	
	return -1; //for debugging
}

void printQueue(struct wait_queue *queue)
{
	int enable = interrupts_set(0);
	if (queue == NULL){interrupts_set(enable); return;}
	struct thread *temp = queue->next;
	printf ("Now printing wait queue:\n");
	while (temp != NULL)
	{
		printf ("%d\n", temp->id);
		temp = temp->next;
	}
	interrupts_set(enable);	
	return;
}

struct lock {
	struct wait_queue *queue;//points to wait queue for that specific lock
	int heldThread;//thread that hold the lock
	bool available;
};

struct lock *
lock_create()
{
	int enable = interrupts_set(0);
	struct lock *lock;

	lock = malloc(sizeof(struct lock));
	assert(lock);

	lock->available = true;
	lock->queue = wait_queue_create();
	lock->heldThread = -1;
	interrupts_set(enable);
	return lock;
}

void
lock_destroy(struct lock *lock)
{
	int enable = interrupts_set(0);
	assert(lock != NULL);

	if (lock->available == false){
		interrupts_set(enable);
		return;
	}
	else{ //the lock hasn't been acquired 
		wait_queue_destroy(lock->queue);
		free(lock);
		interrupts_set(enable);
		return;
	}
	interrupts_set(enable);
	return;
}

void
lock_acquire(struct lock *lock)
{
	int enable = interrupts_set(0);
	assert(lock != NULL);

	while (lock->available == false) //while the lock has already been acquired
	{
		thread_sleep(lock->queue);
	}
	//at this point the thread is ready to acquire the lock
	lock->heldThread = currentThread->id; //lock held by currently running thread
	lock->available = false; //lock has been acquired
	interrupts_set(enable);
	return;
}

void
lock_release(struct lock *lock)
{
	int enable = interrupts_set(0);
	assert(lock != NULL);

	if (lock->heldThread < 0 || lock->heldThread != currentThread->id) //if the release isn't being held by the current thread
	{
		interrupts_set(enable);
		return;
	}
	//else the current thread is the one that's holding the lock
	thread_wakeup(lock->queue, 1); //wakeup all threads in that lock's wait queue
	lock->available = true; //lock is now available
	lock->heldThread = -2; //lock held by no thread
	interrupts_set(enable);
	return;
}

struct cv {
	struct wait_queue *queue;

};

struct cv *
cv_create()
{
	int enable = interrupts_set(0);
	struct cv *cv;

	cv = malloc(sizeof(struct cv));
	assert(cv);

	cv->queue = wait_queue_create();

	interrupts_set(enable);
	return cv;
}

void
cv_destroy(struct cv *cv)
{
	int enable = interrupts_set(0);
	assert(cv != NULL);

	wait_queue_destroy(cv->queue);
	free(cv);

	interrupts_set(enable);
	return;
}

void
cv_wait(struct cv *cv, struct lock *lock)
{
	int enable = interrupts_set(0);
	assert(cv != NULL);
	assert(lock != NULL);

	if (lock->heldThread == currentThread->id) //the current thread is what's holding the lock at this point
	{
		lock_release(lock);
		thread_sleep(cv->queue);
		lock_acquire(lock);
	}
//
	interrupts_set(enable);
	return;
}

void
cv_signal(struct cv *cv, struct lock *lock)
{
	int enable = interrupts_set(0);
	assert(cv != NULL);
	assert(lock != NULL);

	if (lock->heldThread == currentThread->id) //if the caller has acquired the lock
	{
		thread_wakeup(cv->queue, 0); //wakeup one thread
	}

	interrupts_set(enable);
	return;
}

void
cv_broadcast(struct cv *cv, struct lock *lock)
{
	int enable = interrupts_set(0);
	assert(cv != NULL);
	assert(lock != NULL);

	if (lock->heldThread == currentThread->id) //if the caller has acquired the lock
	{
		thread_wakeup(cv->queue, 1); //wakeup one thread
	}

	interrupts_set(enable);
	return;
}
