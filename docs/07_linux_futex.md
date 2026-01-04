# Futex (Fast Userspace Mutex)

A **futex** (fast userspace mutex) is a sophisticated synchronization primitive that optimizes the common case where lock contention is low. The key insight is that most lock acquisitions succeed immediately, so we can avoid expensive kernel syscalls by doing atomic operations in userspace and only invoking the kernel when we actually need to wait.

## Linux Futex

In Linux, a futex operates on a simple integer in shared memory. The basic operations are:

**FUTEX_WAIT**: Atomically check if the futex value equals an expected value, and if so, sleep until woken.

**FUTEX_WAKE**: Wake up one or more threads waiting on the futex.

### How It Works

The two-phase locking strategy:

1. **Fast path (userspace)**: Use atomic compare-and-swap (CAS) to try acquiring the lock. If successful, no kernel involvement at all.
2. **Slow path (kernel)**: If the lock is contended, make a syscall to wait in the kernel's wait queue.

### Linux Example

```c
#include <linux/futex.h>
#include <sys/syscall.h>
#include <stdatomic.h>
#include <unistd.h>
#include <stdio.h>

// Wrapper functions for futex syscalls
static long futex_wait(atomic_int *futex_addr, int expected) {
    return syscall(SYS_futex, futex_addr, FUTEX_WAIT, expected, NULL, NULL, 0);
}

static long futex_wake(atomic_int *futex_addr, int num_to_wake) {
    return syscall(SYS_futex, futex_addr, FUTEX_WAKE, num_to_wake, NULL, NULL, 0);
}

// Simple mutex built on futex
typedef struct {
    atomic_int state;  // 0 = unlocked, 1 = locked no waiters, 2 = locked with waiters
} futex_mutex_t;

void futex_mutex_lock(futex_mutex_t *mutex) {
    int expected = 0;
    
    // Fast path: try to acquire lock with CAS
    if (atomic_compare_exchange_strong(&mutex->state, &expected, 1)) {
        return;  // Success! No kernel involvement needed
    }
    
    // Slow path: contention detected
    do {
        // If state is 2 (waiters exist) or we set it to 2, wait
        if (expected == 2 || 
            atomic_exchange(&mutex->state, 2) != 0) {
            
            // Kernel syscall: put this thread to sleep
            futex_wait(&mutex->state, 2);
        }
        
        expected = 0;
    } while (!atomic_compare_exchange_strong(&mutex->state, &expected, 2));
}

void futex_mutex_unlock(futex_mutex_t *mutex) {
    // Decrement state
    if (atomic_fetch_sub(&mutex->state, 1) != 1) {
        // There were waiters (state was 2), so wake one
        atomic_store(&mutex->state, 0);
        futex_wake(&mutex->state, 1);
    }
}

// Usage example
#include <pthread.h>

futex_mutex_t shared_mutex = { .state = 0 };
int shared_counter = 0;

void* worker_thread(void* arg) {
    for (int i = 0; i < 100000; i++) {
        futex_mutex_lock(&shared_mutex);
        shared_counter++;
        futex_mutex_unlock(&shared_mutex);
    }
    return NULL;
}

int main() {
    pthread_t threads[4];
    
    for (int i = 0; i < 4; i++) {
        pthread_create(&threads[i], NULL, worker_thread, NULL);
    }
    
    for (int i = 0; i < 4; i++) {
        pthread_join(threads[i], NULL);
    }
    
    printf("Final counter: %d\n", shared_counter);
    return 0;
}
```

### Real-World Linux Usage

Most pthread synchronization primitives use futexes internally:
- `pthread_mutex_t` - Uses futex for blocking
- `pthread_cond_t` - Uses futex for wait/signal
- `sem_t` - POSIX semaphores built on futex
- C++ `std::mutex` - Eventually calls futex on Linux

## FreeRTOS

FreeRTOS doesn't have futexes because it's designed for bare-metal embedded systems without userspace/kernel space separation. Instead, it provides **direct kernel primitives** that serve similar purposes but work differently.

### Key Differences

FreeRTOS synchronization primitives are simpler because:
- No userspace/kernel boundary
- Typically single-core or simpler SMP
- Direct manipulation of task queues
- All operations are "kernel" operations

### FreeRTOS Equivalents

The closest equivalents are **mutexes**, **binary semaphores**, and **task notifications**.

```c
#include "FreeRTOS.h"
#include "task.h"
#include "semphr.h"

// Traditional mutex approach
SemaphoreHandle_t xMutex;
int shared_counter = 0;

void vTaskFunction(void *pvParameters) {
    for (int i = 0; i < 1000; i++) {
        // Take mutex (blocks if unavailable)
        if (xSemaphoreTake(xMutex, portMAX_DELAY) == pdTRUE) {
            shared_counter++;
            xSemaphoreGive(xMutex);
        }
    }
    vTaskDelete(NULL);
}

// More efficient: Task notifications (similar to futex concept)
// These are lighter-weight and built into the TCB (Task Control Block)
TaskHandle_t xTaskToNotify;

void vNotifyingTask(void *pvParameters) {
    // Do some work...
    
    // Notify waiting task (similar to futex_wake)
    xTaskNotifyGive(xTaskToNotify);
    
    vTaskDelete(NULL);
}

void vWaitingTask(void *pvParameters) {
    // Wait for notification (similar to futex_wait)
    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
    
    printf("Notification received!\n");
    vTaskDelete(NULL);
}

void app_main() {
    // Create mutex
    xMutex = xSemaphoreCreateMutex();
    
    // Create tasks
    xTaskCreate(vTaskFunction, "Task1", 2048, NULL, 1, NULL);
    xTaskCreate(vTaskFunction, "Task2", 2048, NULL, 1, NULL);
    
    // Task notification example
    xTaskCreate(vWaitingTask, "Waiting", 2048, NULL, 1, &xTaskToNotify);
    xTaskCreate(vNotifyingTask, "Notifying", 2048, NULL, 1, NULL);
    
    vTaskStartScheduler();
}
```

**Task Notifications** in FreeRTOS are conceptually similar to futexes in that they're optimized for the fast path - the notification value is stored directly in the TCB, avoiding separate object allocation.

## RTIC (Real-Time Interrupt-driven Concurrency)

RTIC is a Rust framework for ARM Cortex-M that takes a **fundamentally different approach** - it eliminates the need for traditional mutexes entirely through **static analysis and hardware priorities**.

### The RTIC Philosophy

RTIC uses the processor's **NVIC (Nested Vectored Interrupt Controller)** to provide lock-free resource sharing. There are no futexes because there's no blocking - resources are protected by **priority-based preemption masking**.

### How RTIC Replaces Mutexes

```rust
#[rtic::app(device = stm32f4xx_hal::pac, dispatchers = [EXTI0])]
mod app {
    use stm32f4xx_hal::gpio::{Output, Pin};
    
    #[shared]
    struct Shared {
        // Shared resource - automatically protected
        counter: u32,
    }
    
    #[local]
    struct Local {
        led: Pin<'A', 5, Output>,
    }
    
    #[init]
    fn init(ctx: init::Context) -> (Shared, Local) {
        // Setup hardware...
        (
            Shared { counter: 0 },
            Local { led: /* ... */ },
        )
    }
    
    // High priority task (priority = 2)
    #[task(shared = [counter], priority = 2)]
    async fn high_prio(mut ctx: high_prio::Context) {
        // Access shared resource
        ctx.shared.counter.lock(|counter| {
            *counter += 1;
            // This "lock" doesn't block - it just masks interrupts
            // below priority 2 using the NVIC
        });
    }
    
    // Low priority task (priority = 1)
    #[task(shared = [counter], priority = 1)]
    async fn low_prio(mut ctx: low_prio::Context) {
        // This task can be preempted by high_prio
        ctx.shared.counter.lock(|counter| {
            // NVIC ceiling protocol: masks interrupts at priority <= 2
            // No waiting, no syscalls, just hardware priority masking
            *counter += 10;
        });
    }
}
```

### Why No Futexes in RTIC?

1. **No dynamic blocking**: Tasks don't wait in queues; they're preempted by hardware priority
2. **Compile-time guarantees**: The RTIC framework analyzes priority levels at compile time
3. **Lock-free for compatible priorities**: If priorities don't conflict, no locking needed at all
4. **Hardware-based**: Uses ARM Cortex-M's BASEPRI register to mask interrupts

This is called **Stack Resource Policy (SRP)** or **Immediate Priority Ceiling Protocol**.

## Embassy

Embassy is an async Rust framework for embedded systems. It provides a different model again, using **async/await** with cooperative multitasking.

### Embassy's Approach

Embassy uses Rust's `async` primitives combined with embassy-specific synchronization types. The closest equivalents to futexes are **Signals** and **Mutexes**, but they work in an async context.

### Embassy Mutex Example

```rust
use embassy_executor::Spawner;
use embassy_sync::blocking_mutex::raw::CriticalSectionRawMutex;
use embassy_sync::mutex::Mutex;
use embassy_time::Timer;

// Shared counter protected by mutex
static COUNTER: Mutex<CriticalSectionRawMutex, u32> = Mutex::new(0);

#[embassy_executor::task]
async fn incrementer_task() {
    loop {
        // Async lock - yields to executor if contended
        let mut counter = COUNTER.lock().await;
        *counter += 1;
        
        // Lock is automatically released when dropped
        drop(counter);
        
        Timer::after_millis(100).await;
    }
}

#[embassy_executor::task]
async fn reader_task() {
    loop {
        let counter = COUNTER.lock().await;
        defmt::info!("Counter: {}", *counter);
        drop(counter);
        
        Timer::after_millis(500).await;
    }
}

#[embassy_executor::main]
async fn main(spawner: Spawner) {
    spawner.spawn(incrementer_task()).unwrap();
    spawner.spawn(reader_task()).unwrap();
}
```

### Embassy Signal (Futex-like primitive)

The **Signal** type is very similar to a futex in concept:

```rust
use embassy_sync::signal::Signal;
use embassy_sync::blocking_mutex::raw::CriticalSectionRawMutex;

// Signal is like a single-slot channel
static READY_SIGNAL: Signal<CriticalSectionRawMutex, u32> = Signal::new();

#[embassy_executor::task]
async fn producer_task() {
    embassy_time::Timer::after_secs(2).await;
    
    // Signal with a value (like futex_wake with data)
    READY_SIGNAL.signal(42);
}

#[embassy_executor::task]
async fn consumer_task() {
    defmt::info!("Waiting for signal...");
    
    // Wait for signal (like futex_wait, but async)
    let value = READY_SIGNAL.wait().await;
    
    defmt::info!("Received: {}", value);
}
```

### Key Characteristics

**Async waiting**: When an Embassy mutex or signal is contended, the task `.await`s, yielding control to the executor. This is similar to futex_wait but in an async context.

**No kernel**: Like RTIC, there's no OS kernel. The executor schedules tasks cooperatively.

**Critical sections**: The `CriticalSectionRawMutex` uses interrupt masking for the atomic operations (similar to futex's userspace atomic ops).

## Comparison Summary

| Feature | Linux Futex | FreeRTOS | RTIC | Embassy |
|---------|------------|----------|------|---------|
| **Design** | Userspace atomic + kernel wait | Direct kernel primitives | Hardware priority masking | Async cooperative |
| **Blocking** | Kernel wait queue | Task blocked in queue | No blocking (preemption) | Async yield |
| **Fast path** | Atomic CAS in userspace | Semaphore take | Priority check | Atomic check + yield |
| **Slow path** | Syscall to kernel | Context switch | NVIC masking | Executor reschedule |
| **Overhead** | Low (when uncontended) | Medium | Very low | Low |
| **Use case** | Multi-threaded userspace apps | Traditional RTOS tasks | Hard real-time interrupts | Async embedded apps |

The futex concept of "fast userspace, slow kernel" is unique to systems with user/kernel separation. Embedded systems achieve similar efficiency through different means - hardware priorities (RTIC), lightweight RTOS primitives (FreeRTOS), or async executors (Embassy).