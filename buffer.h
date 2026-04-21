#include <stdatomic.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#ifdef _WIN32
#include <windows.h>
#else
#include <threads.h>
#endif

struct Buffer {
    int* data;
    int size;
    int head;
    int tail;
    atomic_bool can_access; // Atomic variable to track ownership for synchronization
};

enum BufferError {
    BUFFER_SUCCESS = 0,
    BUFFER_INVALID_SIZE,
    BUFFER_FULL,
    BUFFER_EMPTY,
    BUFFER_TAKEN,
    BUFFER_FAILED_MALLOC
};

enum BufferError buffer_init(int size, struct Buffer* buffer);
void buffer_destroy(struct Buffer* buffer);
// Non-blocking push and pop operations
enum BufferError buffer_push(struct Buffer* buffer, int value);
enum BufferError buffer_pop(struct Buffer* buffer, int* value);
// Blocking push and pop operations
enum BufferError buffer_push_blocking(struct Buffer* buffer, int value);
enum BufferError buffer_pop_blocking(struct Buffer* buffer, int* value);