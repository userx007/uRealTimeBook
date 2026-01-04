# Mutexes: Detailed Overview Across Platforms

## Core Concept

A **mutex** (mutual exclusion lock) is a synchronization primitive that protects shared resources from concurrent access. When a thread/task acquires a mutex, other threads must wait until it's released. This prevents race conditions and ensures data consistency.

---

## 1. Linux Mutexes

Linux provides mutexes primarily through **POSIX threads (pthreads)** and the underlying **futex** (fast userspace mutex) system call.

### Key Features:
- **Priority inheritance**: Prevents priority inversion by temporarily elevating the priority of a lock-holding thread
- **Robust mutexes**: Can recover from thread termination while holding a lock
- **Types**: Normal, recursive, error-checking
- **Futex-based**: Minimizes kernel involvement when uncontended

### Usage Example (pthread_mutex):

```c
#include <pthread.h>
#include <stdio.h>
#include <unistd.h>

int shared_counter = 0;
pthread_mutex_t counter_mutex;

void* increment_thread(void* arg) {
    for (int i = 0; i < 100000; i++) {
        pthread_mutex_lock(&counter_mutex);
        shared_counter++;
        pthread_mutex_unlock(&counter_mutex);
    }
    return NULL;
}

int main() {
    pthread_t threads[4];
    
    // Initialize mutex
    pthread_mutex_init(&counter_mutex, NULL);
    
    // Create threads
    for (int i = 0; i < 4; i++) {
        pthread_create(&threads[i], NULL, increment_thread, NULL);
    }
    
    // Wait for completion
    for (int i = 0; i < 4; i++) {
        pthread_join(threads[i], NULL);
    }
    
    printf("Final counter: %d\n", shared_counter);
    
    // Cleanup
    pthread_mutex_destroy(&counter_mutex);
    return 0;
}
```

### Priority Inheritance Example:

```c
pthread_mutexattr_t attr;
pthread_mutex_t mutex;

// Enable priority inheritance
pthread_mutexattr_init(&attr);
pthread_mutexattr_setprotocol(&attr, PTHREAD_PRIO_INHERIT);
pthread_mutex_init(&mutex, &attr);

// Now mutex supports priority inheritance
pthread_mutexattr_destroy(&attr);
```

---

## 2. FreeRTOS Mutexes

FreeRTOS implements mutexes as a special type of semaphore with ownership tracking and priority inheritance.

### Key Features:
- **Priority inheritance**: Built-in support to prevent priority inversion
- **Recursive mutexes**: Tasks can lock the same mutex multiple times
- **Ownership tracking**: Only the task that acquired the mutex can release it
- **Lightweight**: Designed for resource-constrained embedded systems

### Usage Example:

```c
#include "FreeRTOS.h"
#include "task.h"
#include "semphr.h"

SemaphoreHandle_t xMutex;
int shared_resource = 0;

void vTask1(void *pvParameters) {
    while (1) {
        // Wait indefinitely to acquire mutex
        if (xSemaphoreTake(xMutex, portMAX_DELAY) == pdTRUE) {
            // Critical section
            shared_resource++;
            printf("Task1: %d\n", shared_resource);
            
            // Release mutex
            xSemaphoreGive(xMutex);
        }
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

void vTask2(void *pvParameters) {
    while (1) {
        if (xSemaphoreTake(xMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
            shared_resource += 10;
            printf("Task2: %d\n", shared_resource);
            xSemaphoreGive(xMutex);
        }
        vTaskDelay(pdMS_TO_TICKS(150));
    }
}

int main(void) {
    // Create mutex (with priority inheritance)
    xMutex = xSemaphoreCreateMutex();
    
    if (xMutex != NULL) {
        xTaskCreate(vTask1, "Task1", 1000, NULL, 2, NULL);
        xTaskCreate(vTask2, "Task2", 1000, NULL, 2, NULL);
        
        vTaskStartScheduler();
    }
    
    // Should never reach here
    while (1);
}
```

### Recursive Mutex Example:

```c
SemaphoreHandle_t xRecursiveMutex;

void nested_function(void) {
    if (xSemaphoreTakeRecursive(xRecursiveMutex, portMAX_DELAY) == pdTRUE) {
        // Can be called even if mutex already held by this task
        // Do work...
        xSemaphoreGiveRecursive(xRecursiveMutex);
    }
}

void vTask(void *pvParameters) {
    if (xSemaphoreTakeRecursive(xRecursiveMutex, portMAX_DELAY) == pdTRUE) {
        // First lock
        nested_function(); // Second lock (same task)
        xSemaphoreGiveRecursive(xRecursiveMutex);
    }
}

// In main:
xRecursiveMutex = xSemaphoreCreateRecursiveMutex();
```

---

## 3. RTIC (Real-Time Interrupt-driven Concurrency)

RTIC takes a **fundamentally different approach**. Instead of traditional mutexes, it uses:
- **Resource locking** based on priority ceilings (Stack Resource Policy)
- **Compile-time analysis** to prevent deadlocks
- **Hardware-based** critical sections (interrupt masking)

### Key Features:
- **Zero-cost abstraction**: No runtime overhead
- **Deadlock-free by design**: Static analysis at compile time
- **Priority ceiling protocol**: Automatically raises priority when accessing shared resources
- **No dynamic allocation**: Everything resolved at compile time

### Usage Example (RTIC v2):

```rust
#![no_std]
#![no_main]

use rtic::app;

#[app(device = stm32f4xx_hal::pac, dispatchers = [EXTI0])]
mod app {
    use stm32f4xx_hal::pac;
    
    #[shared]
    struct Shared {
        // Shared resource automatically gets mutex-like protection
        counter: u32,
    }
    
    #[local]
    struct Local {}
    
    #[init]
    fn init(ctx: init::Context) -> (Shared, Local) {
        (Shared { counter: 0 }, Local {})
    }
    
    // High priority task
    #[task(shared = [counter], priority = 2)]
    async fn high_prio_task(mut ctx: high_prio_task::Context) {
        // Lock counter with automatic priority ceiling
        ctx.shared.counter.lock(|counter| {
            *counter += 1;
            // Priority automatically raised to prevent preemption
        });
        // Lock released, priority restored
    }
    
    // Low priority task
    #[task(shared = [counter], priority = 1)]
    async fn low_prio_task(mut ctx: low_prio_task::Context) {
        ctx.shared.counter.lock(|counter| {
            *counter += 10;
            // While locked, high_prio_task cannot preempt
            // even though it has higher priority
        });
    }
}
```

### Multi-Resource Locking:

```rust
#[shared]
struct Shared {
    resource_a: u32,
    resource_b: u32,
}

#[task(shared = [resource_a, resource_b])]
async fn task(mut ctx: task::Context) {
    // Lock multiple resources simultaneously
    // Deadlock-free guaranteed by compiler
    (ctx.shared.resource_a, ctx.shared.resource_b).lock(|a, b| {
        *a += 1;
        *b += *a;
    });
}
```

---

## 4. Embassy (Async Rust for Embedded)

Embassy provides **async-aware mutexes** that integrate with its executor, allowing tasks to yield while waiting for locks without blocking.

### Key Features:
- **Async/await syntax**: Non-blocking lock acquisition
- **No priority inheritance needed**: Async nature prevents traditional priority inversion
- **Type-safe**: Rust's type system ensures memory safety
- **Efficient**: Tasks yield to executor while waiting

### Usage Example:

```rust
#![no_std]
#![no_main]

use embassy_executor::Spawner;
use embassy_sync::mutex::Mutex;
use embassy_sync::blocking_mutex::raw::CriticalSectionRawMutex;
use embassy_time::{Duration, Timer};

// Shared resource protected by async mutex
static COUNTER: Mutex<CriticalSectionRawMutex, u32> = Mutex::new(0);

#[embassy_executor::task]
async fn task1() {
    loop {
        // Async lock - yields to executor if unavailable
        let mut counter = COUNTER.lock().await;
        *counter += 1;
        defmt::info!("Task1: {}", *counter);
        drop(counter); // Explicit unlock
        
        Timer::after(Duration::from_millis(100)).await;
    }
}

#[embassy_executor::task]
async fn task2() {
    loop {
        {
            let mut counter = COUNTER.lock().await;
            *counter += 10;
            defmt::info!("Task2: {}", *counter);
            // Automatically unlocked when guard drops
        }
        
        Timer::after(Duration::from_millis(200)).await;
    }
}

#[embassy_executor::main]
async fn main(spawner: Spawner) {
    spawner.spawn(task1()).unwrap();
    spawner.spawn(task2()).unwrap();
}
```

### Blocking Mutex (for interrupt contexts):

```rust
use embassy_sync::blocking_mutex::Mutex as BlockingMutex;
use embassy_sync::blocking_mutex::raw::CriticalSectionRawMutex;

static SHARED: BlockingMutex<CriticalSectionRawMutex, u32> = 
    BlockingMutex::new(0);

// Can be used in interrupt handlers
#[interrupt]
fn TIM2() {
    SHARED.lock(|data| {
        *data += 1;
    });
}
```

### Critical Section-based Protection:

```rust
use embassy_sync::blocking_mutex::raw::CriticalSectionRawMutex;
use embassy_sync::mutex::Mutex;

// For single-core systems
static DATA: Mutex<CriticalSectionRawMutex, SensorData> = 
    Mutex::new(SensorData::new());

async fn sensor_task() {
    loop {
        let mut data = DATA.lock().await;
        data.temperature = read_sensor();
        data.timestamp = now();
    }
}
```

---

## Comparison Summary

| Feature | Linux | FreeRTOS | RTIC | Embassy |
|---------|-------|----------|------|---------|
| **Implementation** | Futex-based | Semaphore variant | Hardware ceiling | Async-aware |
| **Priority Inheritance** | Yes (optional) | Yes (built-in) | N/A (ceiling protocol) | N/A (async) |
| **Overhead** | Low (user-space) | Low | Zero | Low |
| **Deadlock Prevention** | Runtime only | Runtime only | Compile-time | Runtime |
| **Blocking** | Thread blocks | Task blocks | Priority-based | Task yields |
| **Best For** | Multi-threaded apps | Traditional RTOS | Hard real-time | Async embedded |

Each platform's mutex implementation reflects its design philosophy: Linux prioritizes flexibility, FreeRTOS emphasizes simplicity, RTIC guarantees correctness at compile-time, and Embassy leverages Rust's async ecosystem for efficient resource usage.