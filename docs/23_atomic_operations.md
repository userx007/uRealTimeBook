# Atomic Operations: Advanced Synchronization Mechanisms

## Overview

**Atomic operations** are indivisible instructions that complete in a single, uninterruptible step from the perspective of other threads or processors. They provide lock-free synchronization primitives that are essential for building efficient concurrent systems without the overhead of traditional locking mechanisms.

## Core Concepts

### What Makes an Operation Atomic?

An atomic operation guarantees:
1. **Indivisibility**: The operation cannot be observed in a half-completed state
2. **Visibility**: Changes are immediately visible to all threads/cores
3. **Ordering**: Memory ordering guarantees prevent reordering issues

### Hardware Support

Modern CPUs provide special instructions for atomic operations:
- **x86/x64**: `LOCK` prefix, `CMPXCHG` (compare-and-swap), `XCHG`
- **ARM**: `LDREX/STREX` (load/store exclusive), `LDADD` (atomic add)
- **RISC-V**: `LR/SC` (load-reserved/store-conditional), `AMO` instructions

## Platform-Specific Implementations

### 1. Linux Kernel (`atomic_t` and `atomic64_t`)

The Linux kernel provides architecture-independent atomic types:

```c
#include <linux/atomic.h>

// Declaration
atomic_t counter = ATOMIC_INIT(0);
atomic64_t large_counter = ATOMIC64_INIT(0);

// Common operations
atomic_inc(&counter);              // Increment by 1
atomic_dec(&counter);              // Decrement by 1
atomic_add(5, &counter);           // Add 5
int old = atomic_read(&counter);   // Read current value
atomic_set(&counter, 10);          // Set to 10

// Compare-and-swap
int old_val = 5;
int new_val = 10;
if (atomic_cmpxchg(&counter, old_val, new_val) == old_val) {
    // Successfully swapped if counter was 5
}

// Test and set
if (atomic_dec_and_test(&counter)) {
    // Counter reached zero
}
```

**Real-world example**: Reference counting
```c
struct my_object {
    atomic_t refcount;
    void *data;
};

void get_object(struct my_object *obj) {
    atomic_inc(&obj->refcount);
}

void put_object(struct my_object *obj) {
    if (atomic_dec_and_test(&obj->refcount)) {
        // Last reference dropped, safe to free
        kfree(obj->data);
        kfree(obj);
    }
}
```

### 2. Rust (`std::sync::atomic`)

Rust provides safe atomic types with explicit memory ordering:

```rust
use std::sync::atomic::{AtomicBool, AtomicUsize, Ordering};
use std::sync::Arc;
use std::thread;

// Basic atomic types
let counter = AtomicUsize::new(0);
let flag = AtomicBool::new(false);

// Operations with memory ordering
counter.fetch_add(1, Ordering::SeqCst);     // Atomic increment
counter.fetch_sub(1, Ordering::Release);    // Atomic decrement
let value = counter.load(Ordering::Acquire); // Atomic read
counter.store(42, Ordering::SeqCst);        // Atomic write

// Compare-and-swap
let current = counter.load(Ordering::SeqCst);
match counter.compare_exchange(
    current,
    current + 1,
    Ordering::SeqCst,
    Ordering::SeqCst
) {
    Ok(prev) => println!("Updated from {}", prev),
    Err(actual) => println!("Failed, actual value: {}", actual),
}
```

**Real-world example**: Lock-free spinlock
```rust
use std::sync::atomic::{AtomicBool, Ordering};

pub struct SpinLock {
    locked: AtomicBool,
}

impl SpinLock {
    pub fn new() -> Self {
        SpinLock {
            locked: AtomicBool::new(false),
        }
    }
    
    pub fn lock(&self) {
        // Spin until we acquire the lock
        while self.locked.swap(true, Ordering::Acquire) {
            // Busy-wait
            std::hint::spin_loop();
        }
    }
    
    pub fn unlock(&self) {
        self.locked.store(false, Ordering::Release);
    }
}
```

**Embassy/RTIC embedded example**:
```rust
use core::sync::atomic::{AtomicU32, Ordering};

static SENSOR_READING: AtomicU32 = AtomicU32::new(0);

#[embassy_executor::task]
async fn sensor_reader() {
    loop {
        let reading = read_sensor().await;
        SENSOR_READING.store(reading, Ordering::Relaxed);
        Timer::after_millis(100).await;
    }
}

#[embassy_executor::task]
async fn data_processor() {
    loop {
        let value = SENSOR_READING.load(Ordering::Relaxed);
        process_data(value);
        Timer::after_millis(50).await;
    }
}
```

### 3. FreeRTOS

FreeRTOS doesn't have standardized atomic types but uses:

**Critical Sections** (for short operations):
```c
volatile uint32_t shared_counter = 0;

void increment_counter(void) {
    taskENTER_CRITICAL();
    shared_counter++;
    taskEXIT_CRITICAL();
}
```

**Architecture-specific atomics** (ARM Cortex-M):
```c
#include "atomic.h"

// Using ARM exclusive access instructions
uint32_t atomic_increment(volatile uint32_t *ptr) {
    uint32_t old, new;
    do {
        old = __LDREXW(ptr);  // Load exclusive
        new = old + 1;
    } while (__STREXW(new, ptr));  // Store exclusive (retry if failed)
    return old;
}

// Atomic compare-and-swap
bool atomic_compare_exchange(volatile uint32_t *ptr, 
                             uint32_t expected, 
                             uint32_t desired) {
    uint32_t old;
    do {
        old = __LDREXW(ptr);
        if (old != expected) {
            __CLREX();  // Clear exclusive monitor
            return false;
        }
    } while (__STREXW(desired, ptr));
    return true;
}
```

**Real-world example**: Lock-free ring buffer
```c
typedef struct {
    volatile uint32_t head;
    volatile uint32_t tail;
    uint32_t size;
    uint8_t buffer[256];
} RingBuffer;

bool ring_buffer_push(RingBuffer *rb, uint8_t data) {
    uint32_t current_head, next_head;
    
    do {
        current_head = __LDREXW(&rb->head);
        next_head = (current_head + 1) % rb->size;
        
        // Check if full
        if (next_head == rb->tail) {
            __CLREX();
            return false;
        }
    } while (__STREXW(next_head, &rb->head));
    
    rb->buffer[current_head] = data;
    return true;
}
```

## Memory Ordering

Understanding memory ordering is crucial for correct atomic operations:

### Memory Ordering Types

1. **Relaxed**: No ordering guarantees, only atomicity
2. **Acquire**: Prevents reads/writes from moving before this operation
3. **Release**: Prevents reads/writes from moving after this operation
4. **AcqRel**: Combines Acquire and Release
5. **SeqCst**: Sequential consistency - strongest guarantee

**Example showing importance**:
```rust
use std::sync::atomic::{AtomicBool, AtomicU32, Ordering};

static DATA: AtomicU32 = AtomicU32::new(0);
static READY: AtomicBool = AtomicBool::new(false);

// Producer thread
fn producer() {
    DATA.store(42, Ordering::Relaxed);
    READY.store(true, Ordering::Release);  // Ensures DATA write happens-before
}

// Consumer thread
fn consumer() {
    while !READY.load(Ordering::Acquire) {  // Synchronizes with Release
        std::hint::spin_loop();
    }
    let value = DATA.load(Ordering::Relaxed);  // Guaranteed to see 42
    assert_eq!(value, 42);
}
```

## Common Use Cases

### 1. Reference Counting
```rust
struct SharedResource {
    ref_count: AtomicUsize,
    data: Box<[u8]>,
}

impl SharedResource {
    fn acquire(&self) {
        self.ref_count.fetch_add(1, Ordering::Relaxed);
    }
    
    fn release(&self) -> bool {
        self.ref_count.fetch_sub(1, Ordering::Release) == 1
    }
}
```

### 2. Flags and Signals
```c
// Linux kernel style
atomic_t shutdown_flag = ATOMIC_INIT(0);

void request_shutdown(void) {
    atomic_set(&shutdown_flag, 1);
}

void worker_thread(void) {
    while (!atomic_read(&shutdown_flag)) {
        do_work();
    }
}
```

### 3. Lock-Free Counters
```rust
use std::sync::atomic::{AtomicU64, Ordering};

struct Metrics {
    requests: AtomicU64,
    errors: AtomicU64,
}

impl Metrics {
    fn record_request(&self) {
        self.requests.fetch_add(1, Ordering::Relaxed);
    }
    
    fn record_error(&self) {
        self.errors.fetch_add(1, Ordering::Relaxed);
    }
}
```

### 4. State Machines
```c
typedef enum {
    STATE_INIT = 0,
    STATE_RUNNING = 1,
    STATE_STOPPING = 2,
    STATE_STOPPED = 3
} state_t;

atomic_t device_state = ATOMIC_INIT(STATE_INIT);

bool transition_state(state_t expected, state_t new_state) {
    return atomic_cmpxchg(&device_state, expected, new_state) == expected;
}
```

## Advantages

1. **Performance**: No context switching or kernel involvement
2. **Scalability**: Reduced contention compared to locks
3. **Deadlock-free**: Cannot create circular dependencies
4. **Interrupt-safe**: Works in interrupt handlers (with care)
5. **Low latency**: Predictable worst-case timing

## Limitations

1. **Limited operations**: Only simple read-modify-write operations
2. **ABA problem**: Value might change and change back
3. **Complexity**: Harder to reason about than locks
4. **Memory ordering**: Requires deep understanding
5. **Platform-specific**: May need architecture-specific code

## Best Practices

1. **Use highest-level abstraction**: Prefer language-provided atomics
2. **Document memory ordering**: Explain why you chose specific ordering
3. **Test thoroughly**: Race conditions are hard to reproduce
4. **Profile first**: Don't assume lock-free is always faster
5. **Keep it simple**: Complex atomic algorithms are error-prone

Atomic operations are powerful tools for building efficient concurrent systems, but they require careful consideration of memory ordering and platform-specific behavior.