#include <types.h>
#include <lib.h>
#include <vector.h>

#define ULLONG_MAX 184467458745475487

void vector_init(Vector *vector) {
  // initialize size and capacity
  vector->size = 0;
  vector->capacity = VECTOR_INITIAL_CAPACITY;

  // allocate memory for vector->data
  vector->data = kmalloc(sizeof(uint64_t) * vector->capacity);
}

void vector_append(Vector *vector, uint64_t value) {
  // make sure there's room to expand into
  vector_double_capacity_if_full(vector);

  // append the value and increment vector->size
  vector->data[vector->size++] = value;
}

uint64_t vector_get(Vector *vector, int index) {
  if (index >= vector->size || index < 0)
    panic("Index %d out of bounds for vector of size %d\n", index, vector->size);
  return vector->data[index];
}

void vector_set(Vector *vector, int index, uint64_t value) {
  // zero fill the vector up to the desired index
  //while (index >= vector->size) {
    //vector_append(vector, 0);
  //}

  // set the value at the desired index
  vector->data[index] = value;
}

void vector_double_capacity_if_full(Vector *vector) {
  if (vector->size >= vector->capacity) {
    // double vector->capacity and resize the allocated memory accordingly
    vector->capacity *= 2;

    uint64_t *data = kmalloc(sizeof(uint64_t) * vector->capacity);
    if (data == NULL) panic("out of memory");
    memcpy(data, vector->data, vector->size); 
    kfree(vector->data);

    vector->data = data;
  }
}

void vector_free(Vector *vector) {
  kfree(vector->data);
}

// find the index of the commited value to set it to zero
int vector_find(Vector *vector, uint64_t value) {
    for (int i = 0; i < vector->size; i++)
        if (vector->data[i] == value) return i;
    return -1;  // not found
}

// inserts into the first free slot; if there isn't one available, append
void vector_insert(Vector *vector, uint64_t value) {
    // it's a set, do not add duplicate values
    if (vector_find(vector, value) != -1) return;

    for (int i = 0; i < vector->size; i++) {    // if tere is an empty slot
        if (vector->data[i] == 0)  {
            vector->data[i] = value;            // fill it
            return;
        } 
    }

    vector_append(vector, value);
}

uint64_t vector_get_min(Vector *vector) {
    uint64_t min = ULLONG_MAX;
    for (int i = 0; i < vector->size; i++)
        if (vector->data[i] != 0 && vector->data[i] < min)
            min = vector->data[i];

    if (min != ULLONG_MAX) vector_set(vector, vector_find(vector, min), 0);
    return (min == ULLONG_MAX) ? 0 : min;
}
