# Counting Semaphores: A Detailed Overview

## Concept

**Counting semaphores** are synchronization primitives that maintain an integer counter representing the number of available resources. Unlike binary semaphores (which toggle between 0 and 1), counting semaphores can have values from 0 to N, where N represents the maximum number of resources available.

### Key Operations:
- **Wait/P/Decrement**: Decreases the counter. If counter is 0, the caller blocks until a resource becomes available
- **Signal/V/Increment**: Increases the counter, potentially unblocking waiting tasks
- **Initial Value**: Set at creation to represent the initial resource count

### Common Use Cases:
1. **Resource pools** - Managing a fixed number of identical resources (database connections, buffers, hardware units)
2. **Producer-consumer problems** - Tracking available slots in bounded buffers
3. **Rate limiting** - Controlling concurrent access to limited resources
4. **Connection pools** - Managing maximum simultaneous connections

---

## 1. Linux (POSIX Semaphores)

Linux provides counting semaphores through the POSIX semaphore API (`sem_t`).

### Example: Buffer Pool Management

```c
#include <stdio.h>
#include <pthread.h>
#include <semaphore.h>
#include <unistd.h>
#include <stdlib.h>

#define MAX_BUFFERS 5
#define NUM_WORKERS 8

// Pool of shared buffers
typedef struct {
    char data[256];
    int id;
} Buffer;

Buffer buffer_pool[MAX_BUFFERS];
sem_t available_buffers;  // Counting semaphore

void* worker_thread(void* arg) {
    int worker_id = *(int*)arg;
    
    for (int i = 0; i < 3; i++) {
        // Wait for an available buffer (decrements semaphore)
        printf("Worker %d: Waiting for buffer...\n", worker_id);
        sem_wait(&available_buffers);
        
        // Get current semaphore value
        int value;
        sem_getvalue(&available_buffers, &value);
        printf("Worker %d: Got buffer! (Remaining: %d)\n", worker_id, value);
        
        // Simulate work with the buffer
        sleep(rand() % 3 + 1);
        
        // Release the buffer (increments semaphore)
        sem_post(&available_buffers);
        printf("Worker %d: Released buffer\n", worker_id);
    }
    
    return NULL;
}

int main() {
    pthread_t threads[NUM_WORKERS];
    int thread_ids[NUM_WORKERS];
    
    // Initialize counting semaphore with MAX_BUFFERS available
    // sem_init(semaphore, shared_between_processes, initial_value)
    sem_init(&available_buffers, 0, MAX_BUFFERS);
    
    // Create worker threads
    for (int i = 0; i < NUM_WORKERS; i++) {
        thread_ids[i] = i;
        pthread_create(&threads[i], NULL, worker_thread, &thread_ids[i]);
    }
    
    // Wait for all threads to complete
    for (int i = 0; i < NUM_WORKERS; i++) {
        pthread_join(threads[i], NULL);
    }
    
    sem_destroy(&available_buffers);
    printf("All workers completed!\n");
    
    return 0;
}
```

### Named Semaphores (Inter-Process)

```c
#include <fcntl.h>
#include <sys/stat.h>

// For sharing between processes
sem_t* sem = sem_open("/my_counting_sem", O_CREAT, 0644, 5);
sem_wait(sem);  // Acquire
sem_post(sem);  // Release
sem_close(sem);
sem_unlink("/my_counting_sem");
```

---

## 2. FreeRTOS

FreeRTOS provides explicit counting semaphore support with `xSemaphoreCreateCounting()`.

### Example: Managing DMA Channels

```c
#include "FreeRTOS.h"
#include "task.h"
#include "semphr.h"

#define NUM_DMA_CHANNELS 3
#define NUM_TASKS 6

SemaphoreHandle_t dma_channels;

void data_transfer_task(void *pvParameters) {
    int task_id = (int)pvParameters;
    
    for (int i = 0; i < 5; i++) {
        printf("Task %d: Requesting DMA channel...\n", task_id);
        
        // Wait for available DMA channel (blocks if count is 0)
        if (xSemaphoreTake(dma_channels, portMAX_DELAY) == pdTRUE) {
            printf("Task %d: Got DMA channel, transferring data\n", task_id);
            
            // Simulate DMA transfer
            vTaskDelay(pdMS_TO_TICKS(1000));
            
            // Release the DMA channel
            xSemaphoreGive(dma_channels);
            printf("Task %d: Released DMA channel\n", task_id);
        }
        
        vTaskDelay(pdMS_TO_TICKS(500));
    }
    
    vTaskDelete(NULL);
}

void app_main(void) {
    // Create counting semaphore
    // xSemaphoreCreateCounting(max_count, initial_count)
    dma_channels = xSemaphoreCreateCounting(NUM_DMA_CHANNELS, NUM_DMA_CHANNELS);
    
    if (dma_channels == NULL) {
        printf("Failed to create counting semaphore!\n");
        return;
    }
    
    // Create multiple tasks that will compete for DMA channels
    for (int i = 0; i < NUM_TASKS; i++) {
        xTaskCreate(data_transfer_task, "DMA_Task", 2048, (void*)i, 5, NULL);
    }
    
    vTaskStartScheduler();
}
```

### Producer-Consumer with Counting Semaphores

```c
#define QUEUE_SIZE 10

SemaphoreHandle_t empty_slots;  // Tracks empty slots
SemaphoreHandle_t filled_slots; // Tracks filled slots
SemaphoreHandle_t mutex;        // Protects queue access

int queue[QUEUE_SIZE];
int write_idx = 0;
int read_idx = 0;

void producer_task(void *pvParameters) {
    int data = 0;
    
    while (1) {
        // Wait for empty slot
        xSemaphoreTake(empty_slots, portMAX_DELAY);
        
        // Protect queue access
        xSemaphoreTake(mutex, portMAX_DELAY);
        queue[write_idx] = data++;
        write_idx = (write_idx + 1) % QUEUE_SIZE;
        xSemaphoreGive(mutex);
        
        // Signal filled slot
        xSemaphoreGive(filled_slots);
        
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

void consumer_task(void *pvParameters) {
    while (1) {
        // Wait for filled slot
        xSemaphoreTake(filled_slots, portMAX_DELAY);
        
        // Protect queue access
        xSemaphoreTake(mutex, portMAX_DELAY);
        int data = queue[read_idx];
        read_idx = (read_idx + 1) % QUEUE_SIZE;
        xSemaphoreGive(mutex);
        
        // Signal empty slot
        xSemaphoreGive(empty_slots);
        
        printf("Consumed: %d\n", data);
        vTaskDelay(pdMS_TO_TICKS(150));
    }
}

void setup_producer_consumer(void) {
    empty_slots = xSemaphoreCreateCounting(QUEUE_SIZE, QUEUE_SIZE);  // All empty initially
    filled_slots = xSemaphoreCreateCounting(QUEUE_SIZE, 0);          // None filled initially
    mutex = xSemaphoreCreateMutex();
    
    xTaskCreate(producer_task, "Producer", 2048, NULL, 5, NULL);
    xTaskCreate(consumer_task, "Consumer", 2048, NULL, 5, NULL);
}
```

---

## 3. RTIC (Real-Time Interrupt-driven Concurrency)

RTIC doesn't have traditional counting semaphores because it uses a **different concurrency model** based on:
- Static priority-based scheduling
- Resource sharing with priority ceilings
- Message passing via software tasks

However, you can **emulate counting semaphore behavior** using:

### Approach 1: Using Software Tasks with Capacity

```rust
#[rtic::app(device = stm32f4xx_hal::pac, dispatchers = [EXTI0, EXTI1])]
mod app {
    use heapless::spsc::Queue;
    
    const MAX_RESOURCES: usize = 5;
    
    #[shared]
    struct Shared {
        available_count: u32,
    }
    
    #[local]
    struct Local {}
    
    #[init]
    fn init(ctx: init::Context) -> (Shared, Local) {
        // Spawn initial "tokens"
        for _ in 0..MAX_RESOURCES {
            resource_available::spawn().ok();
        }
        
        (
            Shared { available_count: MAX_RESOURCES },
            Local {},
        )
    }
    
    // Represents an available resource token
    #[task(capacity = 5, shared = [available_count])]
    async fn resource_available(mut ctx: resource_available::Context) {
        // This task being spawned represents a resource being available
        // Workers will consume these task instances
    }
    
    #[task(shared = [available_count])]
    async fn worker(mut ctx: worker::Context, id: u32) {
        defmt::info!("Worker {}: Waiting for resource", id);
        
        // Wait for resource_available to be spawnable (counting behavior)
        // In practice, you'd use a channel or similar mechanism
        resource_available::spawn().await.ok();
        
        defmt::info!("Worker {}: Got resource!", id);
        
        // Do work
        cortex_m::asm::delay(1_000_000);
        
        // Release resource by spawning a new token
        resource_available::spawn().ok();
        defmt::info!("Worker {}: Released resource", id);
    }
}
```

### Approach 2: Using Channels (More Idiomatic)

```rust
use rtic_sync::{channel::*, make_channel};

#[rtic::app(device = stm32f4xx_hal::pac, dispatchers = [EXTI0, EXTI1, EXTI2])]
mod app {
    use super::*;
    
    const CHANNEL_CAPACITY: usize = 5;
    
    #[shared]
    struct Shared {}
    
    #[local]
    struct Local {
        resource_sender: Sender<'static, (), CHANNEL_CAPACITY>,
        resource_receiver: Receiver<'static, (), CHANNEL_CAPACITY>,
    }
    
    #[init]
    fn init(ctx: init::Context) -> (Shared, Local) {
        let (sender, receiver) = make_channel!((), CHANNEL_CAPACITY);
        
        // Fill channel with "tokens" representing resources
        for _ in 0..CHANNEL_CAPACITY {
            sender.try_send(()).ok();
        }
        
        // Spawn worker tasks
        for i in 0..8 {
            worker::spawn(i).ok();
        }
        
        (
            Shared {},
            Local {
                resource_sender: sender,
                resource_receiver: receiver,
            },
        )
    }
    
    #[task(local = [resource_receiver, resource_sender])]
    async fn worker(ctx: worker::Context, id: u32) {
        let receiver = ctx.local.resource_receiver;
        let sender = ctx.local.resource_sender;
        
        for _ in 0..3 {
            defmt::info!("Worker {}: Waiting for resource", id);
            
            // Wait for a token (blocks if none available)
            receiver.recv().await.ok();
            
            defmt::info!("Worker {}: Acquired resource", id);
            
            // Simulate work
            cortex_m::asm::delay(1_000_000);
            
            // Return token
            sender.send(()).await.ok();
            defmt::info!("Worker {}: Released resource", id);
        }
    }
}
```

**Why RTIC is Different:**
- RTIC prioritizes **zero-cost abstractions** and **compile-time guarantees**
- Counting semaphores introduce runtime overhead and potential blocking
- RTIC's model encourages **static resource allocation** and **priority-based preemption**
- Channels provide similar functionality with better integration into async/await

---

## 4. Embassy

Embassy is an **async/await-based** embedded framework. It **doesn't provide traditional counting semaphores** because:

1. **Async primitives** are preferred (channels, signals)
2. **Resource management** is handled through ownership and borrows
3. **Blocking operations** conflict with the async executor model

### Alternative 1: Using Channels (Recommended)

```rust
use embassy_sync::channel::Channel;
use embassy_executor::Spawner;
use embassy_time::{Duration, Timer};

// Channel acts like counting semaphore: capacity = max resources
static RESOURCE_POOL: Channel<ThreadModeRawMutex, (), 5> = Channel::new();

#[embassy_executor::task]
async fn initialize_pool() {
    // Fill channel with resource tokens
    for _ in 0..5 {
        RESOURCE_POOL.send(()).await;
    }
}

#[embassy_executor::task]
async fn worker(id: u32) {
    loop {
        log::info!("Worker {}: Waiting for resource", id);
        
        // Receive a token (blocks if none available)
        RESOURCE_POOL.receive().await;
        
        log::info!("Worker {}: Acquired resource", id);
        
        // Do work
        Timer::after(Duration::from_secs(1)).await;
        
        // Return token
        RESOURCE_POOL.send(()).await;
        log::info!("Worker {}: Released resource", id);
        
        Timer::after(Duration::from_millis(500)).await;
    }
}

#[embassy_executor::main]
async fn main(spawner: Spawner) {
    spawner.spawn(initialize_pool()).unwrap();
    
    // Spawn multiple workers competing for resources
    for i in 0..8 {
        spawner.spawn(worker(i)).unwrap();
    }
}
```

### Alternative 2: Semaphore-Like Primitive (Custom Implementation)

```rust
use embassy_sync::blocking_mutex::raw::ThreadModeRawMutex;
use embassy_sync::mutex::Mutex;
use embassy_sync::waitqueue::WakerRegistration;
use core::cell::RefCell;

pub struct CountingSemaphore<const N: usize> {
    state: Mutex<ThreadModeRawMutex, RefCell<SemState>>,
}

struct SemState {
    count: usize,
    wakers: [WakerRegistration; 8], // Fixed size for simplicity
}

impl<const N: usize> CountingSemaphore<N> {
    pub const fn new(initial: usize) -> Self {
        Self {
            state: Mutex::new(RefCell::new(SemState {
                count: initial,
                wakers: [const { WakerRegistration::new() }; 8],
            })),
        }
    }
    
    pub async fn acquire(&self) {
        core::future::poll_fn(|cx| {
            let mut state = self.state.lock().await.borrow_mut();
            
            if state.count > 0 {
                state.count -= 1;
                Poll::Ready(())
            } else {
                // Register waker and wait
                state.wakers[0].register(cx.waker());
                Poll::Pending
            }
        }).await
    }
    
    pub async fn release(&self) {
        let mut state = self.state.lock().await.borrow_mut();
        state.count += 1;
        
        // Wake waiting task
        state.wakers[0].wake();
    }
}

// Usage
static RESOURCES: CountingSemaphore<5> = CountingSemaphore::new(5);

#[embassy_executor::task]
async fn worker_task() {
    RESOURCES.acquire().await;
    // Use resource
    Timer::after(Duration::from_secs(1)).await;
    RESOURCES.release().await;
}
```

### Alternative 3: Using Signals for Simple Cases

```rust
use embassy_sync::signal::Signal;

static RESOURCE_AVAILABLE: Signal<ThreadModeRawMutex, ()> = Signal::new();

// For simple cases where you just need to coordinate access
#[embassy_executor::task]
async fn resource_user() {
    RESOURCE_AVAILABLE.wait().await;
    // Use resource
    RESOURCE_AVAILABLE.signal(());
}
```

---

## Comparison Summary

| Platform | Native Support | Typical Approach | Blocking Behavior |
|----------|---------------|------------------|-------------------|
| **Linux** | ✅ Yes (`sem_t`) | POSIX semaphores | Thread blocks in kernel |
| **FreeRTOS** | ✅ Yes | `xSemaphoreCreateCounting()` | Task blocks, context switch |
| **RTIC** | ❌ No | Channels/capacity-limited tasks | Poll-based async |
| **Embassy** | ❌ No | Channels (recommended) | Async yield, no blocking |

### When to Use Counting Semaphores:

✅ **Good for:**
- Managing pools of identical resources
- Traditional RTOS environments (FreeRTOS, Zephyr)
- Inter-process synchronization (Linux)
- Producer-consumer with bounded buffers

❌ **Consider alternatives in:**
- Modern async frameworks (Embassy, Tokio)
- Zero-copy message passing scenarios
- Systems requiring priority inheritance
- When resource heterogeneity matters

The trend in modern embedded systems (RTIC, Embassy) is toward **channel-based** communication and **ownership-based** resource management rather than traditional semaphores, as these provide better safety guarantees and integrate more naturally with async/await patterns.