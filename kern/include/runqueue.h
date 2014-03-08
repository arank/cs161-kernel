struct queue{
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
