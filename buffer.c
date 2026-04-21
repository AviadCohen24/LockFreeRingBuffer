#include "buffer.h"

bool expected_access = true;
bool desired_access = false;

enum BufferError buffer_init(int size, struct Buffer* buffer) {
    // Implementation for buffer initialization
    if (size <= 0) {
        return BUFFER_INVALID_SIZE; // Invalid size
    }
    buffer->data = malloc(size * sizeof(int)); // Allocate memory for the buffer
    if (buffer->data == NULL) {
        return BUFFER_FAILED_MALLOC; // Failed to allocate memory
    }
    buffer->size = size;
    buffer->head = 0;
    buffer->tail = 0;
    atomic_store(&buffer->can_access, true); // Initialize ownership to true
    return BUFFER_SUCCESS;
}

void buffer_destroy(struct Buffer* buffer) {
    // Implementation for buffer destruction
    free(buffer->data); // Free the memory allocated for the buffer
    free(buffer); // Free the buffer structure itself
    printf("Buffer destroyed\n");
}

enum BufferError buffer_push(struct Buffer* buffer, int value) {
    // Implementation for non-blocking push
    if(buffer->tail == (buffer->head + 1) % buffer->size) {
        return BUFFER_FULL; // Buffer is full, cannot push
    }
    if(atomic_compare_exchange_strong_explicit(&buffer->can_access, &expected_access, &desired_access, memory_order_release, memory_order_relaxed) == false) {
        return BUFFER_TAKEN; // Buffer is taken, cannot push
    }
    buffer->data[buffer->head] = value; // Add value to the buffer
    buffer->head = (buffer->head + 1) % buffer->size; // Move head to the next position
    atomic_store(&buffer->can_access, true); // Set ownership back to true after accessing the buffer
    return BUFFER_SUCCESS; // Push successful
}

enum BufferError buffer_pop(struct Buffer* buffer, int* value) {
    // Implementation for non-blocking pop
    if(buffer->head == buffer->tail) {
        return BUFFER_EMPTY; // Buffer is empty, cannot pop
    }
    if(atomic_compare_exchange_strong_explicit(&buffer->can_access, &expected_access, &desired_access, memory_order_release, memory_order_relaxed) == false) {
        return BUFFER_TAKEN; // Buffer is taken, cannot pop
    }
    *value = buffer->data[buffer->tail]; // Retrieve value from the buffer
    buffer->tail = (buffer->tail + 1) % buffer->size; // Move tail to the next position
    atomic_store(&buffer->can_access, true); // Set ownership back to true after accessing
    return BUFFER_SUCCESS; // Pop successful
}

enum BufferError buffer_push_blocking(struct Buffer* buffer, int value) {
    // Implementation for blocking push
    while(buffer->tail == (buffer->head + 1) % buffer->size) { // Wait until there is space in the buffer
        Yield(); // Yield to allow other threads to run
    }
    while(atomic_compare_exchange_strong_explicit(&buffer->can_access, &expected_access, &desired_access, memory_order_release, memory_order_relaxed) == false) { // Wait until the buffer is available for access
        Yield(); // Yield to allow other threads to run
    }
    buffer->data[buffer->head] = value; // Add value to the buffer
    buffer->head = (buffer->head + 1) % buffer->size; // Move head to the next position
    atomic_store(&buffer->can_access, true); // Set ownership back to true after accessing the buffer
    return BUFFER_SUCCESS; // Push successful
}

enum BufferError buffer_pop_blocking(struct Buffer* buffer, int* value) {
    // Implementation for non-blocking pop
    while(buffer->head == buffer->tail) { // Wait until the buffer is not empty
        Yield(); // Yield to allow other threads to run
    }
    while(atomic_compare_exchange_strong_explicit(&buffer->can_access, &expected_access, &desired_access, memory_order_release, memory_order_relaxed) == false) { // Wait until the buffer is available for access
        Yield(); // Yield to allow other threads to run
    }
    *value = buffer->data[buffer->tail]; // Retrieve value from the buffer
    buffer->tail = (buffer->tail + 1) % buffer->size; // Move tail to the next position
    atomic_store(&buffer->can_access, true); // Set ownership back to true after accessing
    return BUFFER_SUCCESS; // Pop successful
}
