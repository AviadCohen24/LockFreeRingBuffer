/*  ring_buffer.h  —  lock-free MPMC ring buffer
 *  -----------------------------------------------
 *  Single-header library. Usage:
 *
 *    // In exactly ONE .c file:
 *    #define RING_BUFFER_IMPLEMENTATION
 *    #include "ring_buffer.h"
 *
 *    // In every other file that needs the API (declarations only):
 *    #include "ring_buffer.h"
 *
 *  License: MIT
 */

#pragma once

#include <stdatomic.h>
#include <stdlib.h>

#ifdef _WIN32
    #include <windows.h>
#else
    #include <sched.h>
#endif

/* ------------------------------------------------------------------ */
/* Platform abstractions                                               */
/* ------------------------------------------------------------------ */

#ifdef _WIN32
    #define RB_CPU_PAUSE()    YieldProcessor()
    #define RB_THREAD_YIELD() SwitchToThread()
#else
    #if defined(__x86_64__) || defined(__i386__)
        #define RB_CPU_PAUSE()  __asm__ volatile("pause" ::: "memory")
    #else
        #define RB_CPU_PAUSE()  __asm__ volatile("" ::: "memory")
    #endif
    #define RB_THREAD_YIELD() sched_yield()
#endif

/* ------------------------------------------------------------------ */
/* Types                                                               */
/* ------------------------------------------------------------------ */

#define RB_CACHE_LINE 64

/* Each seq slot lives on its own cache line to prevent false sharing
   between threads writing to adjacent slots. */
typedef struct {
    atomic_int val;
    char _pad[RB_CACHE_LINE - sizeof(atomic_int)];
} RbSeqSlot;

/* head and tail are on separate cache lines so producers writing tail
   never invalidate the cache line consumers read head from. */
typedef struct {
    int*       data;
    RbSeqSlot* seq;
    int        size;
    char _pad1[RB_CACHE_LINE - sizeof(int*) - sizeof(RbSeqSlot*) - sizeof(int)];

    atomic_int tail;
    char _pad2[RB_CACHE_LINE - sizeof(atomic_int)];

    atomic_int head;
    char _pad3[RB_CACHE_LINE - sizeof(atomic_int)];
} RingBuffer;

typedef enum {
    RB_OK = 0,
    RB_INVALID_SIZE,
    RB_FULL,
    RB_EMPTY,
    RB_ALLOC_FAIL
} RbError;

/* ------------------------------------------------------------------ */
/* API declarations                                                    */
/* ------------------------------------------------------------------ */

/* Initialize a ring buffer with the given capacity.
   The buffer can hold (size - 1) items. Caller owns the RingBuffer. */
RbError rb_init(int size, RingBuffer* rb);

/* Free memory allocated by rb_init. */
void rb_destroy(RingBuffer* rb);

/* Non-blocking push. Returns RB_FULL immediately if no space. */
RbError rb_push(RingBuffer* rb, int value);

/* Non-blocking pop. Returns RB_EMPTY immediately if no data. */
RbError rb_pop(RingBuffer* rb, int* value);

/* Blocking push. Waits until space is available. MPMC-safe. */
RbError rb_push_blocking(RingBuffer* rb, int value);

/* Blocking pop. Waits until data is available. MPMC-safe. */
RbError rb_pop_blocking(RingBuffer* rb, int* value);

/* ------------------------------------------------------------------ */
/* Implementation                                                      */
/* ------------------------------------------------------------------ */

#ifdef RING_BUFFER_IMPLEMENTATION

static void rb__backoff(int* count) {
    if (*count < 16) {
        for (int i = 0; i < *count; i++)
            RB_CPU_PAUSE();
    } else {
        RB_THREAD_YIELD();
    }
    if (*count < 32) *count *= 2;
}

RbError rb_init(int size, RingBuffer* rb) {
    if (size <= 0)
        return RB_INVALID_SIZE;

    rb->data = malloc(size * sizeof(int));
    if (!rb->data)
        return RB_ALLOC_FAIL;

    rb->seq = malloc(size * sizeof(RbSeqSlot));
    if (!rb->seq) {
        free(rb->data);
        return RB_ALLOC_FAIL;
    }

    for (int i = 0; i < size; i++)
        atomic_init(&rb->seq[i].val, i);

    rb->size = size;
    atomic_init(&rb->head, 0);
    atomic_init(&rb->tail, 0);
    return RB_OK;
}

void rb_destroy(RingBuffer* rb) {
    free(rb->data);
    free(rb->seq);
}

RbError rb_push(RingBuffer* rb, int value) {
    int tail      = atomic_load_explicit(&rb->tail, memory_order_relaxed);
    int next_tail = (tail + 1) % rb->size;
    if (next_tail == atomic_load_explicit(&rb->head, memory_order_acquire))
        return RB_FULL;
    rb->data[tail] = value;
    atomic_store_explicit(&rb->tail, next_tail, memory_order_release);
    return RB_OK;
}

RbError rb_pop(RingBuffer* rb, int* value) {
    int head = atomic_load_explicit(&rb->head, memory_order_relaxed);
    if (head == atomic_load_explicit(&rb->tail, memory_order_acquire))
        return RB_EMPTY;
    *value = rb->data[head];
    atomic_store_explicit(&rb->head, (head + 1) % rb->size, memory_order_release);
    return RB_OK;
}

RbError rb_push_blocking(RingBuffer* rb, int value) {
    int pos  = atomic_fetch_add_explicit(&rb->tail, 1, memory_order_relaxed);
    int slot = pos % rb->size;
    int spin = 1;
    while (atomic_load_explicit(&rb->seq[slot].val, memory_order_acquire) != pos)
        rb__backoff(&spin);
    rb->data[slot] = value;
    atomic_store_explicit(&rb->seq[slot].val, pos + 1, memory_order_release);
    return RB_OK;
}

RbError rb_pop_blocking(RingBuffer* rb, int* value) {
    int pos  = atomic_fetch_add_explicit(&rb->head, 1, memory_order_relaxed);
    int slot = pos % rb->size;
    int spin = 1;
    while (atomic_load_explicit(&rb->seq[slot].val, memory_order_acquire) != pos + 1)
        rb__backoff(&spin);
    *value = rb->data[slot];
    atomic_store_explicit(&rb->seq[slot].val, pos + rb->size, memory_order_release);
    return RB_OK;
}

#endif /* RING_BUFFER_IMPLEMENTATION */
