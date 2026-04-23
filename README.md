# LockFreeRingBuffer

A high-performance, lock-free MPMC (Multi-Producer Multi-Consumer) ring buffer written in C11.  
No mutexes. No dynamic locking. Just atomics, cache-line padding, and exponential backoff.

---

## Features

- **Lock-free MPMC** — multiple producers and consumers run truly in parallel using `atomic_fetch_add` and per-slot sequence numbers
- **Cache-line padded** — `head`, `tail`, and each sequence slot live on separate 64-byte cache lines, eliminating false sharing
- **Exponential backoff** — spins cheaply with CPU pause hints, then yields the thread; no busy-burning on congestion
- **Cross-platform** — Windows (`YieldProcessor` / `SwitchToThread`) and Linux (`pause` / `sched_yield`) via `#ifdef`
- **Header-only** (see `OfficialCLibrary` branch) — drop in a single `.h` file, zero build system required
- **Fully tested** — correctness tests for SPSC and MPMC, plus a head-to-head benchmark vs a mutex implementation

---

## Benchmark

Measured on Windows 11, MinGW GCC 15.2, AMD/Intel x86-64.  
Throughput in **Mops/sec** (millions of operations per second) — higher is better.

### Lock-free vs Mutex

| Buffer | Producers | Consumers | Lock-free (Mops/s) | Mutex (Mops/s) | Speedup |
|--------|-----------|-----------|-------------------|----------------|---------|
| 64     | 1         | 1         | 6.5               | 1.9            | 3.4x    |
| 64     | 2         | 2         | 6.0               | 1.3            | 4.8x    |
| 64     | 4         | 4         | 5.2               | 0.7            | 7.7x    |
| 64     | 8         | 8         | 5.1               | 0.4            | **11.5x** |
| 1024   | 4         | 4         | 12.0              | 1.0            | **12.0x** |
| 1024   | 8         | 8         | 14.0              | 0.6            | **24.2x** |

The advantage grows with thread count — exactly where you need it.

---

## Quick Start

### Header-only (recommended)

```c
// In exactly ONE .c file:
#define RING_BUFFER_IMPLEMENTATION
#include "ring_buffer.h"

// In all other files:
#include "ring_buffer.h"
```

```c
#include <stdio.h>
#define RING_BUFFER_IMPLEMENTATION
#include "ring_buffer.h"

int main(void) {
    RingBuffer rb;
    rb_init(64, &rb);

    rb_push(&rb, 42);

    int val;
    rb_pop(&rb, &val);          // val == 42
    printf("Got: %d\n", val);

    rb_destroy(&rb);
}
```

### CMake

```cmake
add_library(ring_buffer INTERFACE)
target_include_directories(ring_buffer INTERFACE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(ring_buffer INTERFACE Threads::Threads)

target_link_libraries(your_target PRIVATE ring_buffer)
```

---

## API

```c
// Lifecycle
RbError rb_init(int size, RingBuffer* rb);   // allocate — capacity is (size - 1) items
void    rb_destroy(RingBuffer* rb);          // free

// Non-blocking  (single-threaded / SPSC use)
RbError rb_push(RingBuffer* rb, int value);  // returns RB_FULL if no space
RbError rb_pop(RingBuffer* rb, int* value);  // returns RB_EMPTY if no data

// Blocking  (MPMC-safe — spins until space/data is available)
RbError rb_push_blocking(RingBuffer* rb, int value);
RbError rb_pop_blocking(RingBuffer* rb, int* value);
```

### Return codes

| Code             | Meaning                          |
|------------------|----------------------------------|
| `RB_OK`          | Success                          |
| `RB_FULL`        | Buffer full (non-blocking push)  |
| `RB_EMPTY`       | Buffer empty (non-blocking pop)  |
| `RB_INVALID_SIZE`| `size <= 0` passed to `rb_init`  |
| `RB_ALLOC_FAIL`  | `malloc` failed                  |

---

## How It Works

### The MPMC problem

A naive ring buffer uses a single `tail` pointer that producers increment. With multiple producers, two threads can read the same `tail`, both pass the "is full?" check, and write to the same slot — one item is lost and `tail` only advances once. Consumers then wait forever for items that will never arrive.

### The fix: `fetch_add` + per-slot sequence numbers

```
Producer:                        Consumer:
  pos = fetch_add(tail, 1)         pos = fetch_add(head, 1)
  slot = pos % size                slot = pos % size

  wait until seq[slot] == pos      wait until seq[slot] == pos + 1
  data[slot] = value               val  = data[slot]
  seq[slot]  = pos + 1             seq[slot] = pos + size
```

`fetch_add` gives every producer a **unique position** — like pulling a numbered ticket at a deli counter. No two producers ever own the same slot.

`seq[slot]` is the handshake:

| `seq[slot]` value | Meaning                          |
|-------------------|----------------------------------|
| `== pos`          | Slot is empty — safe to write    |
| `== pos + 1`      | Data ready — safe to read        |
| `== pos + size`   | Consumer done — next cycle ready |

### Cache-line padding

`head` and `tail` sit on separate 64-byte cache lines. Without this, every write to `tail` by a producer invalidates the cache line that consumers use to read `head`, causing constant cross-core cache invalidation even though the variables are logically independent.

Each `seq[slot]` entry is also padded to 64 bytes, so producers writing to adjacent slots never fight over the same cache line.

### Backoff strategy

```
spin count < 16  →  CPU pause hint (x86: PAUSE instruction)
spin count >= 16 →  OS thread yield (SwitchToThread / sched_yield)
```

The CPU pause hint reduces pipeline pressure and power during short waits. The OS yield kicks in for genuine congestion, letting other threads make progress instead of burning the CPU.

---

## Building & Testing

```bash
cmake -S . -B build
cmake --build build
./build/RingBufferTest
```

The test suite covers:
- Init / invalid args
- Push / pop / full / empty
- FIFO ordering and wrap-around
- Drain and re-fill cycles
- Concurrent SPSC (2000 items)
- MPMC correctness: 2P2C, 4P4C, 4P1C, 1P4C, 8P8C (sum verification)
- Performance benchmarks across buffer sizes and thread counts
- Head-to-head comparison vs mutex implementation

---

## Branch Structure

| Branch              | Description                                                  |
|---------------------|--------------------------------------------------------------|
| `main`              | SPSC (Single-Producer Single-Consumer) baseline              |
| `MPMC`              | Full MPMC with cache-line padding, backoff, mutex comparison |
| `OfficialCLibrary`  | Header-only library (`ring_buffer.h`) — ready to ship        |

---

## Platform Support

| Platform       | Compiler       | Status  |
|----------------|----------------|---------|
| Windows 11     | MinGW GCC 15.2 | Tested  |
| Linux (x86-64) | GCC / Clang    | Supported via `#ifdef` |
| Linux (ARM)    | GCC / Clang    | Supported (no PAUSE, compiler barrier used) |

Requires **C11** for `<stdatomic.h>`.

---

## License

MIT
