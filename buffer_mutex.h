#pragma once
#include <stdbool.h>
#include <stdlib.h>
#ifdef _WIN32
#include <windows.h>
#else
#include <pthread.h>
#endif

struct BufferMutex {
    int*   data;
    int    size;
    int    head;
    int    tail;
#ifdef _WIN32
    CRITICAL_SECTION lock;
    CONDITION_VARIABLE not_full;
    CONDITION_VARIABLE not_empty;
#else
    pthread_mutex_t lock;
    pthread_cond_t  not_full;
    pthread_cond_t  not_empty;
#endif
};

enum BufferMutexError {
    BUFMTX_SUCCESS = 0,
    BUFMTX_INVALID_SIZE,
    BUFMTX_FULL,
    BUFMTX_EMPTY,
    BUFMTX_FAILED_MALLOC
};

enum BufferMutexError bufmtx_init(int size, struct BufferMutex* b);
void                  bufmtx_destroy(struct BufferMutex* b);
enum BufferMutexError bufmtx_push(struct BufferMutex* b, int value);
enum BufferMutexError bufmtx_pop(struct BufferMutex* b, int* value);
enum BufferMutexError bufmtx_push_blocking(struct BufferMutex* b, int value);
enum BufferMutexError bufmtx_pop_blocking(struct BufferMutex* b, int* value);
