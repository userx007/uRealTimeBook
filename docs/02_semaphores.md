# Semaphores in Real-Time Programming

## What Are Semaphores?

A semaphore is a synchronization primitive used to control access to shared resources in concurrent programming. It acts as a signaling mechanism that allows tasks or threads to coordinate their execution, preventing race conditions and ensuring safe access to critical sections of code. At its core, a semaphore maintains a counter that represents the number of available resources or permissions.

The concept was introduced by Edsger Dijkstra in 1965 and remains fundamental to operating systems and real-time systems today. Semaphores work through two atomic operations: **wait** (often called P, down, or acquire) which decrements the counter, and **signal** (often called V, up, or release) which increments it. When a task attempts to wait on a semaphore with a counter of zero, it blocks until another task signals the semaphore.

## Types of Semaphores

**Binary Semaphores** have only two states: 0 and 1. They're used primarily for signaling between tasks, where one task signals an event and another waits for it. Think of them as a flag that can be raised or lowered. Binary semaphores are ideal for synchronizing task execution, such as when an interrupt handler needs to notify a task that data is ready.

**Counting Semaphores** maintain a counter that can have any non-negative value. They're used to manage a pool of identical resources, where the counter represents the number of available resources. For example, if you have five serial ports available, a counting semaphore initialized to 5 would allow up to five tasks to acquire a port simultaneously. Each acquisition decrements the counter, and each release increments it.

**Mutexes** (mutual exclusion semaphores) are specialized binary semaphores designed specifically for protecting shared resources. While technically similar to binary semaphores, mutexes have important semantic differences: they have ownership (only the task that locked it can unlock it) and often implement priority inheritance to prevent priority inversion problems in real-time systems.

## How Semaphores Work

When a task calls wait on a semaphore, the kernel checks the counter. If it's greater than zero, the counter is decremented and the task continues immediately. If the counter is zero, the task is blocked and placed in a waiting queue associated with that semaphore. The task remains blocked until another task calls signal, which increments the counter and wakes up one waiting task.

The atomicity of these operations is crucial. In single-core systems, this is achieved by disabling interrupts during the operation. In multi-core systems, hardware atomic instructions or locks ensure that the check-and-modify sequence cannot be interrupted.

Priority ordering of waiting tasks varies by system. Some use FIFO ordering, while real-time systems often wake the highest-priority waiting task first to maintain deterministic behavior.

## Platform-Specific Examples

### Linux (POSIX Semaphores)

Linux provides POSIX semaphores in two flavors: named semaphores (accessible across processes) and unnamed semaphores (typically used within a single process between threads).

```c
#include <semaphore.h>
#include <pthread.h>
#include <stdio.h>

sem_t buffer_available;
sem_t buffer_filled;
int shared_buffer;

void* producer(void* arg) {
    for (int i = 0; i < 10; i++) {
        sem_wait(&buffer_available);  // Wait for empty buffer
        
        shared_buffer = i;  // Produce data
        printf("Produced: %d\n", i);
        
        sem_post(&buffer_filled);  // Signal data is ready
    }
    return NULL;
}

void* consumer(void* arg) {
    for (int i = 0; i < 10; i++) {
        sem_wait(&buffer_filled);  // Wait for data
        
        printf("Consumed: %d\n", shared_buffer);
        
        sem_post(&buffer_available);  // Signal buffer is empty
    }
    return NULL;
}

int main() {
    pthread_t prod_thread, cons_thread;
    
    sem_init(&buffer_available, 0, 1);  // Initially available
    sem_init(&buffer_filled, 0, 0);     // Initially empty
    
    pthread_create(&prod_thread, NULL, producer, NULL);
    pthread_create(&cons_thread, NULL, consumer, NULL);
    
    pthread_join(prod_thread, NULL);
    pthread_join(cons_thread, NULL);
    
    sem_destroy(&buffer_available);
    sem_destroy(&buffer_filled);
    
    return 0;
}
```

### FreeRTOS

FreeRTOS provides both binary and counting semaphores, along with mutexes. It's one of the most widely used RTOSes in embedded systems.

```c
#include "FreeRTOS.h"
#include "task.h"
#include "semphr.h"

SemaphoreHandle_t xBinarySemaphore;
SemaphoreHandle_t xMutex;

// Interrupt handler signals task
void UART_IRQHandler(void) {
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;
    
    // Signal that data is available
    xSemaphoreGiveFromISR(xBinarySemaphore, &xHigherPriorityTaskWoken);
    
    portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
}

// Task waits for interrupt signal
void vProcessingTask(void *pvParameters) {
    while(1) {
        // Wait indefinitely for semaphore
        if(xSemaphoreTake(xBinarySemaphore, portMAX_DELAY) == pdTRUE) {
            // Process received data
            process_uart_data();
        }
    }
}

// Using mutex for resource protection
void vSharedResourceTask(void *pvParameters) {
    while(1) {
        if(xSemaphoreTake(xMutex, pdMS_TO_TICKS(100)) == pdTRUE) {
            // Critical section - access shared resource
            modify_shared_data();
            
            xSemaphoreGive(xMutex);
        }
        
        vTaskDelay(pdMS_TO_TICKS(50));
    }
}

int main(void) {
    // Create binary semaphore
    xBinarySemaphore = xSemaphoreCreateBinary();
    
    // Create mutex with priority inheritance
    xMutex = xSemaphoreCreateMutex();
    
    xTaskCreate(vProcessingTask, "Process", 128, NULL, 2, NULL);
    xTaskCreate(vSharedResourceTask, "Task1", 128, NULL, 1, NULL);
    
    vTaskStartScheduler();
    return 0;
}
```

### RTIC (Real-Time Interrupt-driven Concurrency)

RTIC is a Rust framework for ARM Cortex-M microcontrollers that takes a different approach. Instead of traditional semaphores, it uses zero-cost message passing and resource sharing with automatic priority ceiling protocol. However, you can implement semaphore-like behavior using shared resources and software tasks.

```rust
#![no_main]
#![no_std]

use rtic::app;
use panic_halt as _;

#[app(device = stm32f4xx_hal::pac, dispatchers = [EXTI0, EXTI1])]
mod app {
    use heapless::spsc::{Consumer, Producer, Queue};
    
    #[shared]
    struct Shared {
        // Shared resources are protected automatically
        counter: u32,
    }
    
    #[local]
    struct Local {
        producer: Producer<'static, u32, 8>,
        consumer: Consumer<'static, u32, 8>,
    }
    
    #[init]
    fn init(cx: init::Context) -> (Shared, Local) {
        static mut QUEUE: Queue<u32, 8> = Queue::new();
        let (producer, consumer) = QUEUE.split();
        
        // Spawn software tasks
        producer_task::spawn().ok();
        consumer_task::spawn().ok();
        
        (
            Shared { counter: 0 },
            Local { producer, consumer }
        )
    }
    
    // Producer task - runs at priority 1
    #[task(local = [producer], priority = 1)]
    async fn producer_task(cx: producer_task::Context) {
        let producer = cx.local.producer;
        
        for i in 0..10 {
            // Wait until space available (similar to semaphore wait)
            while producer.enqueue(i).is_err() {
                // Could yield here
            }
        }
    }
    
    // Consumer task - runs at priority 2
    #[task(local = [consumer], shared = [counter], priority = 2)]
    async fn consumer_task(mut cx: consumer_task::Context) {
        let consumer = cx.local.consumer;
        
        loop {
            if let Some(value) = consumer.dequeue() {
                // Access shared resource with automatic locking
                cx.shared.counter.lock(|counter| {
                    *counter += value;
                });
            }
        }
    }
}
```

### Embassy

Embassy is an async Rust framework for embedded systems that uses async/await for synchronization rather than traditional semaphores. It provides primitives like `Signal` and `Mutex` that serve similar purposes.

```rust
#![no_std]
#![no_main]

use embassy_executor::Spawner;
use embassy_sync::blocking_mutex::raw::ThreadModeRawMutex;
use embassy_sync::signal::Signal;
use embassy_sync::mutex::Mutex;
use embassy_time::{Duration, Timer};

// Signal acts like a binary semaphore
static DATA_READY: Signal<ThreadModeRawMutex, u32> = Signal::new();

// Mutex for protecting shared resources
static SHARED_COUNTER: Mutex<ThreadModeRawMutex, u32> = Mutex::new(0);

#[embassy_executor::task]
async fn producer_task() {
    let mut count = 0u32;
    loop {
        Timer::after(Duration::from_millis(100)).await;
        
        count += 1;
        // Signal with data (like semaphore post)
        DATA_READY.signal(count);
    }
}

#[embassy_executor::task]
async fn consumer_task() {
    loop {
        // Wait for signal (like semaphore wait)
        let value = DATA_READY.wait().await;
        
        // Access protected resource
        let mut counter = SHARED_COUNTER.lock().await;
        *counter += value;
        
        // Mutex automatically released when 'counter' goes out of scope
    }
}

#[embassy_executor::task]
async fn display_task() {
    loop {
        Timer::after(Duration::from_secs(1)).await;
        
        let counter = SHARED_COUNTER.lock().await;
        defmt::info!("Counter: {}", *counter);
    }
}

#[embassy_executor::main]
async fn main(spawner: Spawner) {
    spawner.spawn(producer_task()).unwrap();
    spawner.spawn(consumer_task()).unwrap();
    spawner.spawn(display_task()).unwrap();
}
```

## Key Considerations

When using semaphores in real-time systems, priority inversion is a critical concern. This occurs when a high-priority task waits for a resource held by a low-priority task, while a medium-priority task preempts the low-priority task. Most modern RTOSes implement priority inheritance protocols where a task temporarily inherits the priority of higher-priority tasks waiting for resources it holds.

The choice between semaphores, mutexes, and other synchronization primitives depends on your use case. Use binary semaphores for signaling events between tasks, counting semaphores for managing resource pools, and mutexes for protecting shared data structures. Modern frameworks like RTIC and Embassy often provide higher-level abstractions that eliminate entire classes of synchronization bugs through their type systems and compile-time guarantees.