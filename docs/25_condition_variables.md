# Condition Variables: Advanced Synchronization for Complex Waiting Conditions

## What Are Condition Variables?

Condition variables are synchronization primitives that allow threads to efficiently wait for complex conditions to become true. Unlike simpler primitives like mutexes (which protect data) or semaphores (which count resources), condition variables enable threads to **sleep until notified that some application-specific condition has changed**, avoiding wasteful busy-waiting or polling.

The key insight is that condition variables work in tandem with a mutex to implement the **monitor pattern**: threads can atomically release a mutex and wait for a condition, then automatically reacquire the mutex when awakened.

## Why Condition Variables Exist

Consider a classic producer-consumer problem with a bounded buffer. You might think to implement it like this:

```c
// Naive approach - DON'T DO THIS
while (buffer_is_full()) {
    unlock_mutex(&buffer_mutex);
    // Busy wait or sleep
    lock_mutex(&buffer_mutex);
}
// Add item to buffer
```

This has serious problems: race conditions (buffer state can change between check and relock), inefficiency (busy waiting or arbitrary sleep times), and complexity (how long to sleep?).

Condition variables solve this elegantly by providing atomic "unlock-and-wait" operations.

## How Condition Variables Work

A condition variable has three fundamental operations:

**1. Wait**: Atomically unlocks the associated mutex and puts the thread to sleep. When awakened, it automatically reacquires the mutex before returning.

**2. Signal (notify_one)**: Wakes up one waiting thread (if any).

**3. Broadcast (notify_all)**: Wakes up all waiting threads.

The standard pattern looks like this:

```c
// Thread waiting for condition
pthread_mutex_lock(&mutex);
while (!condition_is_true) {
    pthread_cond_wait(&cond_var, &mutex);
}
// Condition is now true and mutex is held
do_work();
pthread_mutex_unlock(&mutex);

// Thread changing condition
pthread_mutex_lock(&mutex);
make_condition_true();
pthread_cond_signal(&cond_var);  // or broadcast
pthread_mutex_unlock(&mutex);
```

Note the while loop around the wait—this is **essential** because of spurious wakeups (threads can wake up even when not signaled) and because multiple threads might be racing for the same condition.

## Platform-Specific Implementations

### Linux (POSIX Threads)

Linux provides `pthread_cond_t` with full condition variable semantics:

```c
#include <pthread.h>

typedef struct {
    int buffer[BUFFER_SIZE];
    int count;
    int in;
    int out;
    pthread_mutex_t mutex;
    pthread_cond_t not_empty;
    pthread_cond_t not_full;
} bounded_buffer_t;

void buffer_init(bounded_buffer_t *buf) {
    buf->count = 0;
    buf->in = 0;
    buf->out = 0;
    pthread_mutex_init(&buf->mutex, NULL);
    pthread_cond_init(&buf->not_empty, NULL);
    pthread_cond_init(&buf->not_full, NULL);
}

void buffer_put(bounded_buffer_t *buf, int item) {
    pthread_mutex_lock(&buf->mutex);
    
    // Wait until buffer has space
    while (buf->count == BUFFER_SIZE) {
        pthread_cond_wait(&buf->not_full, &buf->mutex);
    }
    
    // Add item
    buf->buffer[buf->in] = item;
    buf->in = (buf->in + 1) % BUFFER_SIZE;
    buf->count++;
    
    // Signal that buffer is not empty
    pthread_cond_signal(&buf->not_empty);
    pthread_mutex_unlock(&buf->mutex);
}

int buffer_get(bounded_buffer_t *buf) {
    pthread_mutex_lock(&buf->mutex);
    
    // Wait until buffer has data
    while (buf->count == 0) {
        pthread_cond_wait(&buf->not_empty, &buf->mutex);
    }
    
    // Remove item
    int item = buf->buffer[buf->out];
    buf->out = (buf->out + 1) % BUFFER_SIZE;
    buf->count--;
    
    // Signal that buffer is not full
    pthread_cond_signal(&buf->not_full);
    pthread_mutex_unlock(&buf->mutex);
    
    return item;
}
```

This demonstrates the canonical use: two condition variables (one for "not full", one for "not empty") coordinating producer and consumer threads.

### FreeRTOS (Building from Primitives)

FreeRTOS doesn't provide native condition variables, but you can build equivalent functionality using task notifications, semaphores, or queues. Here's an approach using binary semaphores:

```c
// Simulating condition variables in FreeRTOS
typedef struct {
    SemaphoreHandle_t mutex;
    SemaphoreHandle_t wait_sem;
    int waiting_count;
    // Your shared data here
    int buffer[BUFFER_SIZE];
    int count;
} freertos_monitor_t;

void monitor_init(freertos_monitor_t *mon) {
    mon->mutex = xSemaphoreCreateMutex();
    mon->wait_sem = xSemaphoreCreateBinary();
    mon->waiting_count = 0;
    mon->count = 0;
}

void monitor_wait(freertos_monitor_t *mon) {
    mon->waiting_count++;
    xSemaphoreGive(mon->mutex);  // Release mutex
    xSemaphoreTake(mon->wait_sem, portMAX_DELAY);  // Wait
    xSemaphoreTake(mon->mutex, portMAX_DELAY);  // Reacquire
    mon->waiting_count--;
}

void monitor_signal(freertos_monitor_t *mon) {
    if (mon->waiting_count > 0) {
        xSemaphoreGive(mon->wait_sem);
    }
}

// Usage
void producer_task(void *param) {
    freertos_monitor_t *mon = (freertos_monitor_t*)param;
    
    while (1) {
        int item = produce_item();
        
        xSemaphoreTake(mon->mutex, portMAX_DELAY);
        while (mon->count == BUFFER_SIZE) {
            monitor_wait(mon);
        }
        
        mon->buffer[mon->count++] = item;
        monitor_signal(mon);  // Wake consumer
        xSemaphoreGive(mon->mutex);
    }
}
```

This is simplified—a production implementation would need to handle broadcast semantics and be more robust about the wait/signal coordination.

### Embassy (Async Rust Equivalents)

Embassy's async runtime provides condition-variable-like behavior through channels and synchronization primitives, but with async/await semantics:

```rust
use embassy_sync::blocking_mutex::raw::CriticalSectionRawMutex;
use embassy_sync::channel::Channel;
use embassy_sync::mutex::Mutex;

struct BoundedBuffer<const N: usize> {
    buffer: Mutex<CriticalSectionRawMutex, Vec<i32, N>>,
    not_empty: Channel<CriticalSectionRawMutex, (), 1>,
    not_full: Channel<CriticalSectionRawMutex, (), 1>,
}

impl<const N: usize> BoundedBuffer<N> {
    pub fn new() -> Self {
        Self {
            buffer: Mutex::new(Vec::new()),
            not_empty: Channel::new(),
            not_full: Channel::new(),
        }
    }
    
    pub async fn put(&self, item: i32) {
        loop {
            {
                let mut buf = self.buffer.lock().await;
                if buf.len() < N {
                    buf.push(item).ok();
                    drop(buf);
                    self.not_empty.try_send(()).ok();
                    return;
                }
            }
            // Buffer full, wait for space
            self.not_full.receive().await;
        }
    }
    
    pub async fn get(&self) -> i32 {
        loop {
            {
                let mut buf = self.buffer.lock().await;
                if let Some(item) = buf.pop() {
                    drop(buf);
                    self.not_full.try_send(()).ok();
                    return item;
                }
            }
            // Buffer empty, wait for data
            self.not_empty.receive().await;
        }
    }
}
```

In the async world, the `.await` points naturally provide the "release and wait" semantics that condition variables offer in synchronous code.

## Common Patterns and Use Cases

### 1. Producer-Consumer Queue
As shown above—the classic example where producers wait for space and consumers wait for data.

### 2. Thread Pool Work Queue
```c
// Workers wait for work
pthread_mutex_lock(&queue_mutex);
while (queue_is_empty(&work_queue) && !shutdown) {
    pthread_cond_wait(&work_available, &queue_mutex);
}
if (!shutdown) {
    task = dequeue(&work_queue);
}
pthread_mutex_unlock(&queue_mutex);
```

### 3. Barrier Synchronization
Waiting for multiple threads to reach a synchronization point:
```c
pthread_mutex_lock(&barrier_mutex);
arrived_count++;
if (arrived_count == total_threads) {
    arrived_count = 0;
    pthread_cond_broadcast(&barrier_cond);  // Wake everyone
} else {
    pthread_cond_wait(&barrier_cond, &barrier_mutex);
}
pthread_mutex_unlock(&barrier_mutex);
```

### 4. State Machine Coordination
Waiting for a system to enter a specific state before proceeding, common in embedded systems managing hardware state machines or protocol handlers.

## Critical Pitfalls and Best Practices

**Always use a while loop, never if**: Spurious wakeups are real, and multiple threads can be notified simultaneously.

**Hold the mutex when checking conditions**: The condition must be checked while holding the mutex to avoid race conditions.

**Signal/broadcast inside or outside the mutex?** It depends. Signaling inside the critical section is simpler and safer. Signaling outside can reduce lock contention but requires careful analysis.

**Avoid holding locks across expensive operations**: Don't hold the mutex while doing I/O or long computations after your condition is satisfied.

**Deadlock awareness**: If using multiple condition variables, be consistent about lock ordering.

**Broadcast vs. Signal**: Use signal when only one waiter can proceed (like taking an item from a queue). Use broadcast when multiple waiters might be able to proceed or when the condition affects all waiters differently (like a state change that different threads interpret differently).

## Comparison with Other Primitives

**vs. Semaphores**: Semaphores count resources; condition variables wait for arbitrary logical conditions. You can't easily implement "wait until (count > 5 AND flag == true)" with semaphores alone.

**vs. Event Flags/Groups**: Event flags signal that events occurred; condition variables coordinate around shared state that requires mutex protection.

**vs. Busy Waiting**: Condition variables eliminate CPU waste and provide precise wakeup semantics without polling.

**vs. Message Queues**: Message queues transfer data; condition variables coordinate access to shared data structures.

Condition variables shine when you need complex synchronization logic that goes beyond simple counting or flagging, especially when multiple predicates or complex waiting conditions are involved. They're the building block of monitors, one of the fundamental concurrent programming patterns.