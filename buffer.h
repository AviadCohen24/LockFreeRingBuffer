#pragma once

#include <stdatomic.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>

#ifdef _WIN32
    #include <windows.h>
#else
    #include <sched.h>   /* sched_yield() */
#endif

/* ------------------------------------------------------------------ */
/* Platform abstraction                                                */
/* ------------------------------------------------------------------ */

#ifdef _WIN32
    /* CPU-level pause hint: tells the pipeline this is a spin-wait,
       reduces power and improves hyper-threading performance. */
    #define CPU_PAUSE()      YieldProcessor()
    /* OS-level yield: gives up the rest of the current time slice. */
    #define THREAD_YIELD()   SwitchToThread()
#else
    #if defined(__x86_64__) || defined(__i386__)
        #define CPU_PAUSE()  __asm__ volatile("pause" ::: "memory")
    #else
        #define CPU_PAUSE()  __asm__ volatile("" ::: "memory")
    #endif
    #define THREAD_YIELD()   sched_yield()
#endif

/* ------------------------------------------------------------------ */
/* Cache line padding                                                  */
/* ------------------------------------------------------------------ */

#define CACHE_LINE 64

/* Each seq slot padded to its own cache line so adjacent slots don't
   share a line and cause false sharing between threads. */
typedef struct {
    atomic_int val;
    char _pad[CACHE_LINE - sizeof(atomic_int)];
} SeqSlot;

struct Buffer {
    int*     data;
    SeqSlot* seq;
    int      size;

    /* Pad to cache line boundary so tail and head never share a line.
       Producers hammer tail, consumers hammer head — keeping them on
       separate lines stops one side from invalidating the other's cache. */
    char _pad1[CACHE_LINE - sizeof(int*) - sizeof(SeqSlot*) - sizeof(int)];

    atomic_int tail;
    char _pad2[CACHE_LINE - sizeof(atomic_int)];

    atomic_int head;
    char _pad3[CACHE_LINE - sizeof(atomic_int)];
};

/* ------------------------------------------------------------------ */
/* API                                                                 */
/* ------------------------------------------------------------------ */

enum BufferError {
    BUFFER_SUCCESS = 0,
    BUFFER_INVALID_SIZE,
    BUFFER_FULL,
    BUFFER_EMPTY,
    BUFFER_TAKEN,
    BUFFER_FAILED_MALLOC
};

enum BufferError buffer_init(int size, struct Buffer* buffer);
void             buffer_destroy(struct Buffer* buffer);
enum BufferError buffer_push(struct Buffer* buffer, int value);
enum BufferError buffer_pop(struct Buffer* buffer, int* value);
enum BufferError buffer_push_blocking(struct Buffer* buffer, int value);
enum BufferError buffer_pop_blocking(struct Buffer* buffer, int* value);
