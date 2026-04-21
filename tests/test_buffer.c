#include <stdio.h>
#include <stdlib.h>
#include "../buffer.h"

#include <windows.h>

static int g_tests_run = 0;
static int g_tests_passed = 0;

#define ASSERT(cond, msg)                                          \
    do {                                                           \
        g_tests_run++;                                             \
        if (!(cond)) {                                             \
            printf("FAIL [line %d]: %s\n", __LINE__, (msg));      \
        } else {                                                   \
            g_tests_passed++;                                      \
            printf("PASS: %s\n", (msg));                          \
        }                                                          \
    } while (0)

/* ------------------------------------------------------------------ */
/* Init tests                                                          */
/* ------------------------------------------------------------------ */

static void test_init_valid(void) {
    struct Buffer b;
    ASSERT(buffer_init(5, &b) == BUFFER_SUCCESS, "init(5) returns BUFFER_SUCCESS");
}

static void test_init_zero(void) {
    struct Buffer b;
    ASSERT(buffer_init(0, &b) == BUFFER_INVALID_SIZE, "init(0) returns BUFFER_INVALID_SIZE");
}

static void test_init_negative(void) {
    struct Buffer b;
    ASSERT(buffer_init(-4, &b) == BUFFER_INVALID_SIZE, "init(-4) returns BUFFER_INVALID_SIZE");
}

static void test_init_sets_fields(void) {
    struct Buffer b;
    buffer_init(8, &b);
    ASSERT(b.size == 8, "init sets size field");
    ASSERT(b.head == 0, "init sets head to 0");
    ASSERT(b.tail == 0, "init sets tail to 0");
}

/* ------------------------------------------------------------------ */
/* Basic push / pop                                                    */
/* ------------------------------------------------------------------ */

static void test_push_to_empty(void) {
    struct Buffer b;
    buffer_init(5, &b);
    ASSERT(buffer_push(&b, 42) == BUFFER_SUCCESS, "push to empty buffer succeeds");
}

static void test_pop_gives_pushed_value(void) {
    struct Buffer b;
    buffer_init(5, &b);
    buffer_push(&b, 99);
    int val = 0;
    ASSERT(buffer_pop(&b, &val) == BUFFER_SUCCESS, "pop from non-empty succeeds");
    ASSERT(val == 99, "popped value equals pushed value");
}

static void test_pop_empty_returns_error(void) {
    struct Buffer b;
    buffer_init(5, &b);
    int val;
    ASSERT(buffer_pop(&b, &val) == BUFFER_EMPTY, "pop from empty buffer returns BUFFER_EMPTY");
}

/* ------------------------------------------------------------------ */
/* Full / overflow                                                     */
/* ------------------------------------------------------------------ */

static void test_push_full_returns_error(void) {
    /* size=3 ring buffer holds at most 2 items (one slot wasted) */
    struct Buffer b;
    buffer_init(3, &b);
    ASSERT(buffer_push(&b, 1) == BUFFER_SUCCESS, "push 1st item succeeds");
    ASSERT(buffer_push(&b, 2) == BUFFER_SUCCESS, "push 2nd item succeeds");
    ASSERT(buffer_push(&b, 3) == BUFFER_FULL,    "push to full buffer returns BUFFER_FULL");
}

static void test_pop_after_full_succeeds(void) {
    struct Buffer b;
    buffer_init(3, &b);
    buffer_push(&b, 10);
    buffer_push(&b, 20);
    int val;
    buffer_pop(&b, &val);
    ASSERT(buffer_push(&b, 30) == BUFFER_SUCCESS, "push after partial drain succeeds");
}

/* ------------------------------------------------------------------ */
/* FIFO ordering                                                       */
/* ------------------------------------------------------------------ */

static void test_fifo_order(void) {
    struct Buffer b;
    buffer_init(6, &b);
    for (int i = 0; i < 5; i++)
        buffer_push(&b, i * 10);

    int ok = 1;
    for (int i = 0; i < 5; i++) {
        int val;
        buffer_pop(&b, &val);
        if (val != i * 10) ok = 0;
    }
    ASSERT(ok, "FIFO order preserved over 5 pushes/pops");
}

/* ------------------------------------------------------------------ */
/* Wrap-around                                                         */
/* ------------------------------------------------------------------ */

static void test_wraparound(void) {
    /* size=4 holds 3 items; advance head/tail past the array boundary */
    struct Buffer b;
    buffer_init(4, &b);
    buffer_push(&b, 1);
    buffer_push(&b, 2);
    int dummy;
    buffer_pop(&b, &dummy);
    buffer_push(&b, 3);
    buffer_push(&b, 4);

    int v[3];
    buffer_pop(&b, &v[0]);
    buffer_pop(&b, &v[1]);
    buffer_pop(&b, &v[2]);
    ASSERT(v[0] == 2 && v[1] == 3 && v[2] == 4, "wrap-around preserves FIFO order");
}

/* ------------------------------------------------------------------ */
/* Drain and re-fill                                                   */
/* ------------------------------------------------------------------ */

static void test_empty_after_drain(void) {
    struct Buffer b;
    buffer_init(4, &b);
    buffer_push(&b, 7);
    buffer_push(&b, 8);
    int val;
    buffer_pop(&b, &val);
    buffer_pop(&b, &val);
    ASSERT(buffer_pop(&b, &val) == BUFFER_EMPTY, "buffer reports empty after full drain");
}

static void test_refill_after_drain(void) {
    struct Buffer b;
    buffer_init(4, &b);
    buffer_push(&b, 1);
    int val;
    buffer_pop(&b, &val);
    ASSERT(buffer_push(&b, 2) == BUFFER_SUCCESS, "push succeeds after drain");
    buffer_pop(&b, &val);
    ASSERT(val == 2, "correct value after drain-refill cycle");
}

/* ------------------------------------------------------------------ */
/* Concurrent single-producer / single-consumer                       */
/* ------------------------------------------------------------------ */

#define TRANSFER_COUNT 2000

typedef struct {
    struct Buffer* buf;
    int count;
    int result; /* consumer writes its sum here */
} ThreadArgs;

static DWORD WINAPI producer_fn(LPVOID arg) {
    ThreadArgs* a = (ThreadArgs*)arg;
    for (int i = 0; i < a->count; i++)
        buffer_push_blocking(a->buf, i);
    return 0;
}

static DWORD WINAPI consumer_fn(LPVOID arg) {
    ThreadArgs* a = (ThreadArgs*)arg;
    int sum = 0;
    for (int i = 0; i < a->count; i++) {
        int val;
        buffer_pop_blocking(a->buf, &val);
        sum += val;
    }
    a->result = sum;
    return 0;
}

static void test_concurrent_spsc(void) {
    struct Buffer b;
    buffer_init(32, &b);
    ThreadArgs args = { .buf = &b, .count = TRANSFER_COUNT, .result = 0 };

    HANDLE prod = CreateThread(NULL, 0, producer_fn, &args, 0, NULL);
    HANDLE cons = CreateThread(NULL, 0, consumer_fn, &args, 0, NULL);
    WaitForSingleObject(prod, INFINITE);
    WaitForSingleObject(cons, INFINITE);
    CloseHandle(prod);
    CloseHandle(cons);

    int expected = (TRANSFER_COUNT * (TRANSFER_COUNT - 1)) / 2;
    ASSERT(args.result == expected, "concurrent SPSC transfers all items with correct sum");
}

/* ------------------------------------------------------------------ */
/* Entry point                                                         */
/* ------------------------------------------------------------------ */

int main(void) {
    printf("=== init ===\n");
    test_init_valid();
    test_init_zero();
    test_init_negative();
    test_init_sets_fields();

    printf("\n=== push / pop ===\n");
    test_push_to_empty();
    test_pop_gives_pushed_value();
    test_pop_empty_returns_error();

    printf("\n=== full / overflow ===\n");
    test_push_full_returns_error();
    test_pop_after_full_succeeds();

    printf("\n=== ordering ===\n");
    test_fifo_order();

    printf("\n=== wrap-around ===\n");
    test_wraparound();

    printf("\n=== drain / refill ===\n");
    test_empty_after_drain();
    test_refill_after_drain();

    printf("\n=== concurrent SPSC ===\n");
    test_concurrent_spsc();

    printf("\n%d/%d tests passed\n", g_tests_passed, g_tests_run);
    return (g_tests_passed == g_tests_run) ? 0 : 1;
}