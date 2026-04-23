#include <stdio.h>

#define RING_BUFFER_IMPLEMENTATION
#include "ring_buffer.h"

int main() {
    RingBuffer* rb = malloc(sizeof(RingBuffer));
    if (!rb) {
        printf("Failed to allocate buffer\n");
        return 1;
    }
    if (rb_init(5, rb) != RB_OK) {
        printf("Failed to initialize buffer\n");
        free(rb);
        return 1;
    }

    if (rb_push(rb, 10) == RB_OK)
        printf("Pushed 10 to buffer\n");
    else
        printf("Failed to push to buffer\n");

    int value;
    if (rb_pop(rb, &value) == RB_OK)
        printf("Popped %d from buffer\n", value);
    else
        printf("Failed to pop from buffer\n");

    rb_destroy(rb);
    free(rb);
    return 0;
}
