#include "buffer_mutex.h"

/* ------------------------------------------------------------------ */
/* Thin platform wrappers                                              */
/* ------------------------------------------------------------------ */

static void mtx_lock(struct BufferMutex* b)   { EnterCriticalSection(&b->lock); }
static void mtx_unlock(struct BufferMutex* b) { LeaveCriticalSection(&b->lock); }

static void wait_not_full(struct BufferMutex* b) {
    SleepConditionVariableCS(&b->not_full, &b->lock, INFINITE);
}
static void wait_not_empty(struct BufferMutex* b) {
    SleepConditionVariableCS(&b->not_empty, &b->lock, INFINITE);
}
static void signal_not_full(struct BufferMutex* b)  { WakeConditionVariable(&b->not_full); }
static void signal_not_empty(struct BufferMutex* b) { WakeConditionVariable(&b->not_empty); }

/* ------------------------------------------------------------------ */
/* Helpers                                                             */
/* ------------------------------------------------------------------ */

static int is_full(struct BufferMutex* b) {
    return (b->tail + 1) % b->size == b->head;
}
static int is_empty(struct BufferMutex* b) {
    return b->head == b->tail;
}

/* ------------------------------------------------------------------ */
/* API                                                                 */
/* ------------------------------------------------------------------ */

enum BufferMutexError bufmtx_init(int size, struct BufferMutex* b) {
    if (size <= 0) return BUFMTX_INVALID_SIZE;
    b->data = malloc(size * sizeof(int));
    if (!b->data) return BUFMTX_FAILED_MALLOC;
    b->size = size;
    b->head = 0;
    b->tail = 0;
    InitializeCriticalSection(&b->lock);
    InitializeConditionVariable(&b->not_full);
    InitializeConditionVariable(&b->not_empty);
    return BUFMTX_SUCCESS;
}

void bufmtx_destroy(struct BufferMutex* b) {
    free(b->data);
    DeleteCriticalSection(&b->lock);
}

/* Non-blocking push: returns BUFMTX_FULL immediately if no space. */
enum BufferMutexError bufmtx_push(struct BufferMutex* b, int value) {
    mtx_lock(b);
    if (is_full(b)) {
        mtx_unlock(b);
        return BUFMTX_FULL;
    }
    b->data[b->tail] = value;
    b->tail = (b->tail + 1) % b->size;
    signal_not_empty(b);
    mtx_unlock(b);
    return BUFMTX_SUCCESS;
}

/* Non-blocking pop: returns BUFMTX_EMPTY immediately if nothing to read. */
enum BufferMutexError bufmtx_pop(struct BufferMutex* b, int* value) {
    mtx_lock(b);
    if (is_empty(b)) {
        mtx_unlock(b);
        return BUFMTX_EMPTY;
    }
    *value = b->data[b->head];
    b->head = (b->head + 1) % b->size;
    signal_not_full(b);
    mtx_unlock(b);
    return BUFMTX_SUCCESS;
}

/* Blocking push: waits until there is space, then writes. */
enum BufferMutexError bufmtx_push_blocking(struct BufferMutex* b, int value) {
    mtx_lock(b);
    while (is_full(b))
        wait_not_full(b);          /* releases lock while waiting */
    b->data[b->tail] = value;
    b->tail = (b->tail + 1) % b->size;
    signal_not_empty(b);
    mtx_unlock(b);
    return BUFMTX_SUCCESS;
}

/* Blocking pop: waits until there is data, then reads. */
enum BufferMutexError bufmtx_pop_blocking(struct BufferMutex* b, int* value) {
    mtx_lock(b);
    while (is_empty(b))
        wait_not_empty(b);         /* releases lock while waiting */
    *value = b->data[b->head];
    b->head = (b->head + 1) % b->size;
    signal_not_full(b);
    mtx_unlock(b);
    return BUFMTX_SUCCESS;
}
