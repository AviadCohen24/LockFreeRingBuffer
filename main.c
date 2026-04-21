#include "buffer.h"

int main() {
    struct Buffer* buffer = malloc(sizeof(struct Buffer));
    if (buffer == NULL) {
        printf("Failed to allocate buffer\n");
        return 1; // Exit with error code
    }
    if (buffer_init(5, buffer) != BUFFER_SUCCESS) {
        printf("Failed to initialize buffer: Invalid size\n");
        free(buffer);
        return 1; // Exit with error code
    }

    // Example usage of the buffer
    if (buffer_push(buffer, 10) == BUFFER_SUCCESS) {
        printf("Pushed 10 to buffer\n");
    } else {
        printf("Failed to push to buffer\n");
    }

    int value;
    if (buffer_pop(buffer, &value) == BUFFER_SUCCESS) {
        printf("Popped %d from buffer\n", value);
    } else {
        printf("Failed to pop from buffer\n");
    }

    buffer_destroy(buffer); // Clean up the buffer
    return 0; // Exit with success code
}