#include <types.h>
#include <lib.h>
#include <current.h>
#include <cpu.h>
#include <runqueue.h>

void mlfq_add(struct mlfq *fq, struct thread *t){
    //kprintf("adding thread of priority %d\n",t->priority);
    if (t->priority >= 0 && t->priority < MAX_PRIORITY){
        threadlist_addtail(&fq->mlfq[t->priority],t);
    }
}

struct thread *mlfq_remhead(struct mlfq *fq){
    int i;
    for (i = 0; i < MAX_PRIORITY; i++){
        if (!threadlist_isempty(&fq->mlfq[i]))
            return threadlist_remhead(&fq->mlfq[i]);
    }
    return NULL;
}

struct thread *mlfq_remtail(struct mlfq *fq){
    int i;
    for (i = MAX_PRIORITY - 1; i >= 0; i--){
        if (!threadlist_isempty(&fq->mlfq[i]))
            return threadlist_remtail(&fq->mlfq[i]);
    }
    return NULL;
}

bool mlfq_isempty(struct mlfq *fq){
    int ret = 0;
    for (int i = 0; i < MAX_PRIORITY; i++)
        ret &= threadlist_isempty(&fq->mlfq[i]);
    return ret;
}

unsigned mlfq_count(struct mlfq *fq){
    unsigned count = 0;
    for (int i = 0; i < MAX_PRIORITY; i++)
        count += fq->mlfq[i].tl_count;
    return count;
}

/* no longer used */

struct queue* init_queue(int max_size)
{
		struct queue *q;
		q = kmalloc(sizeof (struct queue));
        q->first = 0;
        q->last = max_size-1;
        q->count = 0;
        q->q = kmalloc((max_size+1)*sizeof(struct thread*));
        return q;
}

bool enqueue(struct queue *q, struct thread *x)
{
        if (q->count >= q->max){
        	return false;
        }

		q->last = (q->last+1) % q->max;
		q->q[ q->last ] = x;
		q->count = q->count + 1;
		return true;
}

struct thread* dequeue(struct queue *q)
{
		struct thread *x;
        if (q->count <= 0){
        	x = NULL;
        } else {
                x = q->q[ q->first ];
                q->first = (q->first+1) % q->max;
                q->count = q->count - 1;
        }

        return x;
}

bool is_empty(struct queue *q)
{
        if (q->count <= 0) return true;
        return false;
}

// Destroy queue, assuming all threads have detached
void queue_destroy(struct queue *q)
{
	kfree(q->q);
	kfree(q);
}

