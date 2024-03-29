#ifndef _VECTOR_H_
#define _VECTOR_H_

#define VECTOR_INITIAL_CAPACITY 64

// Define a vector type
typedef struct {
    int size;      // slots used so far
    int capacity;  // total available slots
    uint64_t *data;     // array of integers we're storing
} Vector;

void vector_init(Vector *vector);

void vector_append(Vector *vector, uint64_t value);

uint64_t vector_get(Vector *vector, int index);

void vector_set(Vector *vector, int index, uint64_t value);

void vector_double_capacity_if_full(Vector *vector);

void vector_free(Vector *vector);

uint64_t vector_get_min(Vector *vector);
int vector_find(Vector *vector, uint64_t value);
void vector_insert(Vector *vector, uint64_t value);
#endif
