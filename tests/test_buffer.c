#include <stdio.h>
#include <stdlib.h>

#define RING_BUFFER_IMPLEMENTATION
#include "../ring_buffer.h"
#include "../buffer_mutex.h"

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
    RingBuffer b;
    ASSERT(rb_init(5, &b) == RB_OK, "init(5) returns RB_OK");
}

static void test_init_zero(void) {
    RingBuffer b;
    ASSERT(rb_init(0, &b) == RB_INVALID_SIZE, "init(0) returns RB_INVALID_SIZE");
}

static void test_init_negative(void) {
    RingBuffer b;
    ASSERT(rb_init(-4, &b) == RB_INVALID_SIZE, "init(-4) returns RB_INVALID_SIZE");
}

static void test_init_sets_fields(void) {
    RingBuffer b;
    rb_init(8, &b);
    ASSERT(b.size == 8, "init sets size field");
    ASSERT(b.head == 0, "init sets head to 0");
    ASSERT(b.tail == 0, "init sets tail to 0");
}

/* ------------------------------------------------------------------ */
/* Basic push / pop                                                    */
/* ------------------------------------------------------------------ */

static void test_push_to_empty(void) {
    RingBuffer b;
    rb_init(5, &b);
    ASSERT(rb_push(&b, 42) == RB_OK, "push to empty buffer succeeds");
}

static void test_pop_gives_pushed_value(void) {
    RingBuffer b;
    rb_init(5, &b);
    rb_push(&b, 99);
    int val = 0;
    ASSERT(rb_pop(&b, &val) == RB_OK, "pop from non-empty succeeds");
    ASSERT(val == 99, "popped value equals pushed value");
}

static void test_pop_empty_returns_error(void) {
    RingBuffer b;
    rb_init(5, &b);
    int val;
    ASSERT(rb_pop(&b, &val) == RB_EMPTY, "pop from empty buffer returns RB_EMPTY");
}

/* ------------------------------------------------------------------ */
/* Full / overflow                                                     */
/* ------------------------------------------------------------------ */

static void test_push_full_returns_error(void) {
    /* size=3 ring buffer holds at most 2 items (one slot wasted) */
    RingBuffer b;
    rb_init(3, &b);
    ASSERT(rb_push(&b, 1) == RB_OK, "push 1st item succeeds");
    ASSERT(rb_push(&b, 2) == RB_OK, "push 2nd item succeeds");
    ASSERT(rb_push(&b, 3) == RB_FULL,    "push to full buffer returns RB_FULL");
}

static void test_pop_after_full_succeeds(void) {
    RingBuffer b;
    rb_init(3, &b);
    rb_push(&b, 10);
    rb_push(&b, 20);
    int val;
    rb_pop(&b, &val);
    ASSERT(rb_push(&b, 30) == RB_OK, "push after partial drain succeeds");
}

/* ------------------------------------------------------------------ */
/* FIFO ordering                                                       */
/* ------------------------------------------------------------------ */

static void test_fifo_order(void) {
    RingBuffer b;
    rb_init(6, &b);
    for (int i = 0; i < 5; i++)
        rb_push(&b, i * 10);

    int ok = 1;
    for (int i = 0; i < 5; i++) {
        int val;
        rb_pop(&b, &val);
        if (val != i * 10) ok = 0;
    }
    ASSERT(ok, "FIFO order preserved over 5 pushes/pops");
}

/* ------------------------------------------------------------------ */
/* Wrap-around                                                         */
/* ------------------------------------------------------------------ */

static void test_wraparound(void) {
    /* size=4 holds 3 items; advance head/tail past the array boundary */
    RingBuffer b;
    rb_init(4, &b);
    rb_push(&b, 1);
    rb_push(&b, 2);
    int dummy;
    rb_pop(&b, &dummy);
    rb_push(&b, 3);
    rb_push(&b, 4);

    int v[3];
    rb_pop(&b, &v[0]);
    rb_pop(&b, &v[1]);
    rb_pop(&b, &v[2]);
    ASSERT(v[0] == 2 && v[1] == 3 && v[2] == 4, "wrap-around preserves FIFO order");
}

/* ------------------------------------------------------------------ */
/* Drain and re-fill                                                   */
/* ------------------------------------------------------------------ */

static void test_empty_after_drain(void) {
    RingBuffer b;
    rb_init(4, &b);
    rb_push(&b, 7);
    rb_push(&b, 8);
    int val;
    rb_pop(&b, &val);
    rb_pop(&b, &val);
    ASSERT(rb_pop(&b, &val) == RB_EMPTY, "buffer reports empty after full drain");
}

static void test_refill_after_drain(void) {
    RingBuffer b;
    rb_init(4, &b);
    rb_push(&b, 1);
    int val;
    rb_pop(&b, &val);
    ASSERT(rb_push(&b, 2) == RB_OK, "push succeeds after drain");
    rb_pop(&b, &val);
    ASSERT(val == 2, "correct value after drain-refill cycle");
}

/* ------------------------------------------------------------------ */
/* Concurrent single-producer / single-consumer                       */
/* ------------------------------------------------------------------ */

#define TRANSFER_COUNT 2000

typedef struct {
    RingBuffer* buf;
    int count;
    int result; /* consumer writes its sum here */
} ThreadArgs;

static DWORD WINAPI producer_fn(LPVOID arg) {
    ThreadArgs* a = (ThreadArgs*)arg;
    for (int i = 0; i < a->count; i++)
        rb_push_blocking(a->buf, i);
    return 0;
}

static DWORD WINAPI consumer_fn(LPVOID arg) {
    ThreadArgs* a = (ThreadArgs*)arg;
    int sum = 0;
    for (int i = 0; i < a->count; i++) {
        int val;
        rb_pop_blocking(a->buf, &val);
        sum += val;
    }
    a->result = sum;
    return 0;
}

static void test_concurrent_spsc(void) {
    RingBuffer b;
    rb_init(32, &b);
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

/* ================================================================== */
/* MPMC (Multi-Producer Multi-Consumer) correctness tests             */
/*                                                                    */
/* NOTE: requires buffer->head and buffer->tail to be atomic, and    */
/* expected_access to be a thread-local (not a global) bool so that  */
/* concurrent CAS operations don't race on the expected value.        */
/* ================================================================== */

#define MPMC_ITEMS_PER_PRODUCER 500  /* must be divisible by any nc used below */

typedef struct {
    RingBuffer* buf;
    int            start_val;
    int            count;
    long           sum_out;
} MpProducerArgs;

typedef struct {
    RingBuffer* buf;
    int            count;
    long           sum_out;
} McConsumerArgs;

static DWORD WINAPI mpmc_producer_fn(LPVOID arg) {
    MpProducerArgs* a = (MpProducerArgs*)arg;
    long sum = 0;
    for (int i = 0; i < a->count; i++) {
        rb_push_blocking(a->buf, a->start_val + i);
        sum += a->start_val + i;
    }
    a->sum_out = sum;
    return 0;
}

static DWORD WINAPI mpmc_consumer_fn(LPVOID arg) {
    McConsumerArgs* a = (McConsumerArgs*)arg;
    long sum = 0;
    for (int i = 0; i < a->count; i++) {
        int val;
        rb_pop_blocking(a->buf, &val);
        sum += val;
    }
    a->sum_out = sum;
    return 0;
}

/* Spawn np producers and nc consumers, then verify pushed_sum == popped_sum. */
static void run_mpmc_correctness(int np, int nc, int buf_size, const char* label) {
    int items_per_producer = MPMC_ITEMS_PER_PRODUCER;
    int total              = np * items_per_producer;
    int items_per_consumer = total / nc;

    RingBuffer b;
    rb_init(buf_size, &b);

    MpProducerArgs p_args[8];
    McConsumerArgs c_args[8];
    HANDLE         p_handles[8], c_handles[8];

    for (int i = 0; i < np; i++) {
        p_args[i] = (MpProducerArgs){
            .buf = &b, .start_val = i * items_per_producer,
            .count = items_per_producer, .sum_out = 0
        };
        p_handles[i] = CreateThread(NULL, 0, mpmc_producer_fn, &p_args[i], 0, NULL);
    }
    for (int i = 0; i < nc; i++) {
        c_args[i] = (McConsumerArgs){ .buf = &b, .count = items_per_consumer, .sum_out = 0 };
        c_handles[i] = CreateThread(NULL, 0, mpmc_consumer_fn, &c_args[i], 0, NULL);
    }

    WaitForMultipleObjects(np, p_handles, TRUE, INFINITE);
    WaitForMultipleObjects(nc, c_handles, TRUE, INFINITE);
    for (int i = 0; i < np; i++) CloseHandle(p_handles[i]);
    for (int i = 0; i < nc; i++) CloseHandle(c_handles[i]);

    long pushed_sum = 0, popped_sum = 0;
    for (int i = 0; i < np; i++) pushed_sum += p_args[i].sum_out;
    for (int i = 0; i < nc; i++) popped_sum += c_args[i].sum_out;

    ASSERT(pushed_sum == popped_sum, label);
}

static void test_mpmc_2p2c(void) { run_mpmc_correctness(2, 2,  64, "2P2C: pushed sum == popped sum"); }
static void test_mpmc_4p4c(void) { run_mpmc_correctness(4, 4,  64, "4P4C: pushed sum == popped sum"); }
static void test_mpmc_4p1c(void) { run_mpmc_correctness(4, 1, 128, "4P1C: pushed sum == popped sum"); }
static void test_mpmc_1p4c(void) { run_mpmc_correctness(1, 4,  64, "1P4C: pushed sum == popped sum"); }
static void test_mpmc_8p8c(void) { run_mpmc_correctness(8, 8, 256, "8P8C: pushed sum == popped sum"); }

/* ================================================================== */
/* Performance benchmarks                                             */
/*                                                                    */
/* Measures throughput (Mops/sec) across producer/consumer counts     */
/* and buffer sizes to find the best-performing configuration.        */
/* ================================================================== */

#define PERF_ITEMS_PER_PRODUCER 8000  /* divisible by 1/2/4/8 */

typedef struct {
    RingBuffer* buf;
    int            start_val;
    int            count;
} PerfProducerArgs;

typedef struct {
    RingBuffer* buf;
    int            count;
} PerfConsumerArgs;

static DWORD WINAPI perf_producer_fn(LPVOID arg) {
    PerfProducerArgs* a = (PerfProducerArgs*)arg;
    for (int i = 0; i < a->count; i++)
        rb_push_blocking(a->buf, a->start_val + i);
    return 0;
}

static DWORD WINAPI perf_consumer_fn(LPVOID arg) {
    PerfConsumerArgs* a = (PerfConsumerArgs*)arg;
    int val;
    for (int i = 0; i < a->count; i++)
        rb_pop_blocking(a->buf, &val);
    return 0;
}

/* Returns throughput in Mops/sec. */
static double run_perf_benchmark(int np, int nc, int buf_size) {
    int total              = np * PERF_ITEMS_PER_PRODUCER;
    int items_per_consumer = total / nc;

    RingBuffer b;
    rb_init(buf_size, &b);

    PerfProducerArgs p_args[8];
    PerfConsumerArgs c_args[8];
    HANDLE           p_handles[8], c_handles[8];

    LARGE_INTEGER freq, t0, t1;
    QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&t0);

    for (int i = 0; i < np; i++) {
        p_args[i] = (PerfProducerArgs){
            .buf = &b, .start_val = i * PERF_ITEMS_PER_PRODUCER,
            .count = PERF_ITEMS_PER_PRODUCER
        };
        p_handles[i] = CreateThread(NULL, 0, perf_producer_fn, &p_args[i], 0, NULL);
    }
    for (int i = 0; i < nc; i++) {
        c_args[i] = (PerfConsumerArgs){ .buf = &b, .count = items_per_consumer };
        c_handles[i] = CreateThread(NULL, 0, perf_consumer_fn, &c_args[i], 0, NULL);
    }

    WaitForMultipleObjects(np, p_handles, TRUE, INFINITE);
    WaitForMultipleObjects(nc, c_handles, TRUE, INFINITE);
    QueryPerformanceCounter(&t1);

    for (int i = 0; i < np; i++) CloseHandle(p_handles[i]);
    for (int i = 0; i < nc; i++) CloseHandle(c_handles[i]);

    double elapsed = (double)(t1.QuadPart - t0.QuadPart) / (double)freq.QuadPart;
    return (double)total / elapsed / 1e6;
}

static void test_performance(void) {
    static const int configs[][2] = {
        {1,1}, {2,2}, {4,4}, {8,8}, {4,1}, {1,4}
    };
    static const int buf_sizes[] = { 16, 64, 256, 1024 };
    static const int n_configs   = 6;
    static const int n_sizes     = 4;

    printf("\n%-10s %-8s %-8s %12s\n", "BufSize", "Prod", "Cons", "Mops/sec");
    printf("%-10s %-8s %-8s %12s\n",   "-------", "----", "----", "--------");

    double best_mops = 0.0;
    int    best_np = 0, best_nc = 0, best_buf = 0;

    for (int si = 0; si < n_sizes; si++) {
        for (int ci = 0; ci < n_configs; ci++) {
            int np   = configs[ci][0];
            int nc   = configs[ci][1];
            int bsz  = buf_sizes[si];
            double m = run_perf_benchmark(np, nc, bsz);
            printf("%-10d %-8d %-8d %11.3f\n", bsz, np, nc, m);
            if (m > best_mops) { best_mops = m; best_np = np; best_nc = nc; best_buf = bsz; }
        }
    }

    printf("\n>> Best: %dP / %dC / buf=%d  =>  %.3f Mops/sec\n",
           best_np, best_nc, best_buf, best_mops);
    ASSERT(best_mops > 0.0, "performance benchmark completed");
}

/* ================================================================== */
/* Mutex version — correctness tests                                  */
/* ================================================================== */

static void test_mutex_init_valid(void) {
    struct BufferMutex b;
    ASSERT(bufmtx_init(5, &b) == BUFMTX_SUCCESS, "[mtx] init(5) returns BUFMTX_SUCCESS");
    bufmtx_destroy(&b);
}

static void test_mutex_init_zero(void) {
    struct BufferMutex b;
    ASSERT(bufmtx_init(0, &b) == BUFMTX_INVALID_SIZE, "[mtx] init(0) returns BUFMTX_INVALID_SIZE");
}

static void test_mutex_push_pop(void) {
    struct BufferMutex b;
    bufmtx_init(5, &b);
    ASSERT(bufmtx_push(&b, 42) == BUFMTX_SUCCESS, "[mtx] push to empty succeeds");
    int val = 0;
    ASSERT(bufmtx_pop(&b, &val) == BUFMTX_SUCCESS, "[mtx] pop from non-empty succeeds");
    ASSERT(val == 42, "[mtx] popped value equals pushed value");
    bufmtx_destroy(&b);
}

static void test_mutex_pop_empty(void) {
    struct BufferMutex b;
    bufmtx_init(5, &b);
    int val;
    ASSERT(bufmtx_pop(&b, &val) == BUFMTX_EMPTY, "[mtx] pop from empty returns BUFMTX_EMPTY");
    bufmtx_destroy(&b);
}

static void test_mutex_push_full(void) {
    struct BufferMutex b;
    bufmtx_init(3, &b);
    ASSERT(bufmtx_push(&b, 1) == BUFMTX_SUCCESS, "[mtx] push 1st item succeeds");
    ASSERT(bufmtx_push(&b, 2) == BUFMTX_SUCCESS, "[mtx] push 2nd item succeeds");
    ASSERT(bufmtx_push(&b, 3) == BUFMTX_FULL,    "[mtx] push to full returns BUFMTX_FULL");
    bufmtx_destroy(&b);
}

static void test_mutex_fifo_order(void) {
    struct BufferMutex b;
    bufmtx_init(6, &b);
    for (int i = 0; i < 5; i++) bufmtx_push(&b, i * 10);
    int ok = 1;
    for (int i = 0; i < 5; i++) {
        int val;
        bufmtx_pop(&b, &val);
        if (val != i * 10) ok = 0;
    }
    ASSERT(ok, "[mtx] FIFO order preserved");
    bufmtx_destroy(&b);
}

/* MPMC correctness — mutex version */

typedef struct { struct BufferMutex* buf; int start_val; int count; long sum_out; } MtxProdArgs;
typedef struct { struct BufferMutex* buf; int count;     long sum_out;            } MtxConsArgs;

static DWORD WINAPI mtx_producer_fn(LPVOID arg) {
    MtxProdArgs* a = (MtxProdArgs*)arg;
    long sum = 0;
    for (int i = 0; i < a->count; i++) {
        bufmtx_push_blocking(a->buf, a->start_val + i);
        sum += a->start_val + i;
    }
    a->sum_out = sum;
    return 0;
}

static DWORD WINAPI mtx_consumer_fn(LPVOID arg) {
    MtxConsArgs* a = (MtxConsArgs*)arg;
    long sum = 0;
    for (int i = 0; i < a->count; i++) {
        int val;
        bufmtx_pop_blocking(a->buf, &val);
        sum += val;
    }
    a->sum_out = sum;
    return 0;
}

static void run_mutex_mpmc(int np, int nc, int buf_size, const char* label) {
    int items_per_producer = MPMC_ITEMS_PER_PRODUCER;
    int total              = np * items_per_producer;
    int items_per_consumer = total / nc;

    struct BufferMutex b;
    bufmtx_init(buf_size, &b);

    MtxProdArgs p_args[8];
    MtxConsArgs c_args[8];
    HANDLE      p_handles[8], c_handles[8];

    for (int i = 0; i < np; i++) {
        p_args[i] = (MtxProdArgs){ .buf = &b, .start_val = i * items_per_producer,
                                   .count = items_per_producer, .sum_out = 0 };
        p_handles[i] = CreateThread(NULL, 0, mtx_producer_fn, &p_args[i], 0, NULL);
    }
    for (int i = 0; i < nc; i++) {
        c_args[i] = (MtxConsArgs){ .buf = &b, .count = items_per_consumer, .sum_out = 0 };
        c_handles[i] = CreateThread(NULL, 0, mtx_consumer_fn, &c_args[i], 0, NULL);
    }

    WaitForMultipleObjects(np, p_handles, TRUE, INFINITE);
    WaitForMultipleObjects(nc, c_handles, TRUE, INFINITE);
    for (int i = 0; i < np; i++) CloseHandle(p_handles[i]);
    for (int i = 0; i < nc; i++) CloseHandle(c_handles[i]);

    long pushed_sum = 0, popped_sum = 0;
    for (int i = 0; i < np; i++) pushed_sum += p_args[i].sum_out;
    for (int i = 0; i < nc; i++) popped_sum += c_args[i].sum_out;

    ASSERT(pushed_sum == popped_sum, label);
    bufmtx_destroy(&b);
}

static void test_mutex_mpmc_2p2c(void) { run_mutex_mpmc(2, 2,  64, "[mtx] 2P2C: pushed sum == popped sum"); }
static void test_mutex_mpmc_4p4c(void) { run_mutex_mpmc(4, 4,  64, "[mtx] 4P4C: pushed sum == popped sum"); }
static void test_mutex_mpmc_4p1c(void) { run_mutex_mpmc(4, 1, 128, "[mtx] 4P1C: pushed sum == popped sum"); }
static void test_mutex_mpmc_1p4c(void) { run_mutex_mpmc(1, 4,  64, "[mtx] 1P4C: pushed sum == popped sum"); }
static void test_mutex_mpmc_8p8c(void) { run_mutex_mpmc(8, 8, 256, "[mtx] 8P8C: pushed sum == popped sum"); }

/* ================================================================== */
/* Head-to-head benchmark: lock-free vs mutex                         */
/* ================================================================== */

#define CMP_ITEMS_PER_PRODUCER 8000

typedef struct { struct BufferMutex* buf; int start_val; int count; } CmpMtxProd;
typedef struct { struct BufferMutex* buf; int count;                 } CmpMtxCons;

static DWORD WINAPI cmp_mtx_prod_fn(LPVOID arg) {
    CmpMtxProd* a = (CmpMtxProd*)arg;
    for (int i = 0; i < a->count; i++) bufmtx_push_blocking(a->buf, a->start_val + i);
    return 0;
}
static DWORD WINAPI cmp_mtx_cons_fn(LPVOID arg) {
    CmpMtxCons* a = (CmpMtxCons*)arg;
    int val;
    for (int i = 0; i < a->count; i++) bufmtx_pop_blocking(a->buf, &val);
    return 0;
}

static double bench_lockfree(int np, int nc, int buf_size) {
    int total              = np * CMP_ITEMS_PER_PRODUCER;
    int items_per_consumer = total / nc;

    RingBuffer b;
    rb_init(buf_size, &b);

    PerfProducerArgs p_args[8];
    PerfConsumerArgs c_args[8];
    HANDLE           p_handles[8], c_handles[8];

    LARGE_INTEGER freq, t0, t1;
    QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&t0);

    for (int i = 0; i < np; i++) {
        p_args[i] = (PerfProducerArgs){ .buf = &b, .start_val = i * CMP_ITEMS_PER_PRODUCER,
                                        .count = CMP_ITEMS_PER_PRODUCER };
        p_handles[i] = CreateThread(NULL, 0, perf_producer_fn, &p_args[i], 0, NULL);
    }
    for (int i = 0; i < nc; i++) {
        c_args[i] = (PerfConsumerArgs){ .buf = &b, .count = items_per_consumer };
        c_handles[i] = CreateThread(NULL, 0, perf_consumer_fn, &c_args[i], 0, NULL);
    }
    WaitForMultipleObjects(np, p_handles, TRUE, INFINITE);
    WaitForMultipleObjects(nc, c_handles, TRUE, INFINITE);
    QueryPerformanceCounter(&t1);
    for (int i = 0; i < np; i++) CloseHandle(p_handles[i]);
    for (int i = 0; i < nc; i++) CloseHandle(c_handles[i]);

    double elapsed = (double)(t1.QuadPart - t0.QuadPart) / (double)freq.QuadPart;
    return (double)total / elapsed / 1e6;
}

static double bench_mutex(int np, int nc, int buf_size) {
    int total              = np * CMP_ITEMS_PER_PRODUCER;
    int items_per_consumer = total / nc;

    struct BufferMutex b;
    bufmtx_init(buf_size, &b);

    CmpMtxProd p_args[8];
    CmpMtxCons c_args[8];
    HANDLE     p_handles[8], c_handles[8];

    LARGE_INTEGER freq, t0, t1;
    QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&t0);

    for (int i = 0; i < np; i++) {
        p_args[i] = (CmpMtxProd){ .buf = &b, .start_val = i * CMP_ITEMS_PER_PRODUCER,
                                   .count = CMP_ITEMS_PER_PRODUCER };
        p_handles[i] = CreateThread(NULL, 0, cmp_mtx_prod_fn, &p_args[i], 0, NULL);
    }
    for (int i = 0; i < nc; i++) {
        c_args[i] = (CmpMtxCons){ .buf = &b, .count = items_per_consumer };
        c_handles[i] = CreateThread(NULL, 0, cmp_mtx_cons_fn, &c_args[i], 0, NULL);
    }
    WaitForMultipleObjects(np, p_handles, TRUE, INFINITE);
    WaitForMultipleObjects(nc, c_handles, TRUE, INFINITE);
    QueryPerformanceCounter(&t1);
    for (int i = 0; i < np; i++) CloseHandle(p_handles[i]);
    for (int i = 0; i < nc; i++) CloseHandle(c_handles[i]);

    bufmtx_destroy(&b);
    double elapsed = (double)(t1.QuadPart - t0.QuadPart) / (double)freq.QuadPart;
    return (double)total / elapsed / 1e6;
}

static void test_comparison_benchmark(void) {
    static const int configs[][2] = { {1,1}, {2,2}, {4,4}, {8,8}, {4,1}, {1,4} };
    static const int buf_sizes[]  = { 64, 256, 1024 };
    static const int n_configs    = 6;
    static const int n_sizes      = 3;

    printf("\n%-6s %-4s %-4s  %12s  %12s  %8s\n",
           "Buf", "P", "C", "LockFree", "Mutex", "Speedup");
    printf("%-6s %-4s %-4s  %12s  %12s  %8s\n",
           "---", "-", "-", "--------", "-----", "-------");

    for (int si = 0; si < n_sizes; si++) {
        for (int ci = 0; ci < n_configs; ci++) {
            int    np  = configs[ci][0];
            int    nc  = configs[ci][1];
            int    bsz = buf_sizes[si];
            double lf  = bench_lockfree(np, nc, bsz);
            double mtx = bench_mutex(np, nc, bsz);
            printf("%-6d %-4d %-4d  %11.3f  %11.3f  %7.2fx\n",
                   bsz, np, nc, lf, mtx, (mtx > 0.0 ? lf / mtx : 0.0));
        }
    }
    ASSERT(1, "head-to-head benchmark completed");
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

    printf("\n=== MPMC correctness ===\n");
    test_mpmc_2p2c();
    test_mpmc_4p4c();
    test_mpmc_4p1c();
    test_mpmc_1p4c();
    test_mpmc_8p8c();

    printf("\n=== performance benchmarks ===\n");
    test_performance();

    printf("\n=== mutex correctness ===\n");
    test_mutex_init_valid();
    test_mutex_init_zero();
    test_mutex_push_pop();
    test_mutex_pop_empty();
    test_mutex_push_full();
    test_mutex_fifo_order();

    printf("\n=== mutex MPMC correctness ===\n");
    test_mutex_mpmc_2p2c();
    test_mutex_mpmc_4p4c();
    test_mutex_mpmc_4p1c();
    test_mutex_mpmc_1p4c();
    test_mutex_mpmc_8p8c();

    printf("\n=== lock-free vs mutex benchmark ===\n");
    printf("(Mops/sec — higher is better, Speedup = LockFree / Mutex)\n");
    test_comparison_benchmark();

    printf("\n%d/%d tests passed\n", g_tests_passed, g_tests_run);
    return (g_tests_passed == g_tests_run) ? 0 : 1;
}