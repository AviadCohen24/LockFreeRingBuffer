#include "buffer.h"

/* Exponential backoff: spin a few times cheaply, then yield the thread.
   Keeps the fast path fast and stops burning the bus on full/empty waits. */
static void backoff(int* count) {
    if (*count < 16) {
        for (int i = 0; i < *count; i++)
            CPU_PAUSE();
    } else {
        THREAD_YIELD();
    }
    if (*count < 32) *count *= 2;
}

enum BufferError buffer_init(int size, struct Buffer* buffer) {
    if (size <= 0)
        return BUFFER_INVALID_SIZE;

    buffer->data = malloc(size * sizeof(int));
    if (!buffer->data)
        return BUFFER_FAILED_MALLOC;

    buffer->seq = malloc(size * sizeof(SeqSlot));
    if (!buffer->seq) {
        free(buffer->data);
        return BUFFER_FAILED_MALLOC;
    }

    for (int i = 0; i < size; i++)
        atomic_init(&buffer->seq[i].val, i);

    buffer->size = size;
    atomic_init(&buffer->head, 0);
    atomic_init(&buffer->tail, 0);
    return BUFFER_SUCCESS;
}

void buffer_destroy(struct Buffer* buffer) {
    free(buffer->data);
    free(buffer->seq);
}

enum BufferError buffer_push(struct Buffer* buffer, int value) {
    int tail = atomic_load_explicit(&buffer->tail, memory_order_relaxed);
    int next_tail = (tail + 1) % buffer->size;
    if (next_tail == atomic_load_explicit(&buffer->head, memory_order_acquire))
        return BUFFER_FULL;
    buffer->data[tail] = value;
    atomic_store_explicit(&buffer->tail, next_tail, memory_order_release);
    return BUFFER_SUCCESS;
}

enum BufferError buffer_pop(struct Buffer* buffer, int* value) {
    int head = atomic_load_explicit(&buffer->head, memory_order_relaxed);
    if (head == atomic_load_explicit(&buffer->tail, memory_order_acquire))
        return BUFFER_EMPTY;
    *value = buffer->data[head];
    atomic_store_explicit(&buffer->head, (head + 1) % buffer->size, memory_order_release);
    return BUFFER_SUCCESS;
}

enum BufferError buffer_push_blocking(struct Buffer* buffer, int value) {
    int pos  = atomic_fetch_add_explicit(&buffer->tail, 1, memory_order_relaxed);
    int slot = pos % buffer->size;
    int spin = 1;
    while (atomic_load_explicit(&buffer->seq[slot].val, memory_order_acquire) != pos)
        backoff(&spin);
    buffer->data[slot] = value;
    atomic_store_explicit(&buffer->seq[slot].val, pos + 1, memory_order_release);
    return BUFFER_SUCCESS;
}

enum BufferError buffer_pop_blocking(struct Buffer* buffer, int* value) {
    int pos  = atomic_fetch_add_explicit(&buffer->head, 1, memory_order_relaxed);
    int slot = pos % buffer->size;
    int spin = 1;
    while (atomic_load_explicit(&buffer->seq[slot].val, memory_order_acquire) != pos + 1)
        backoff(&spin);
    *value = buffer->data[slot];
    atomic_store_explicit(&buffer->seq[slot].val, pos + buffer->size, memory_order_release);
    return BUFFER_SUCCESS;
}
