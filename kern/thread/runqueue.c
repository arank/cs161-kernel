#include <types.h>
#include <lib.h>
#include <current.h>
#include <runqueue.h>

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

