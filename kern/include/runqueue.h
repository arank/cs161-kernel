#ifndef _RUNQUEUE_H_
#define _RUNQUEUE_H_

#define MAX_PRIORITY 5

struct mlfq {
    struct threadlist mlfq[MAX_PRIORITY];
};

struct queue {
	int first;
	int last;
	int count;
	int max;
	struct thread** q;
};

struct queue* init_queue(int max_size);
bool enqueue(struct queue *q, struct thread *x);
struct thread* dequeue(struct queue *q);
bool is_empty(struct queue *q);
void queue_destroy(struct queue *q);

void mlfq_add(struct mlfq *fq, struct thread *t);
struct thread *mlfq_remhead(struct mlfq *fq);
struct thread *mlfq_remtail(struct mlfq *fq);
bool mlfq_isempty(struct mlfq *fq);
unsigned mlfq_count(struct mlfq *fq);

#endif

