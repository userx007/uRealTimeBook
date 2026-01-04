# Binary Semaphores: Signaling Between Tasks and Threads

## Concept and Purpose

Binary semaphores are synchronization primitives that can exist in one of two states: available (1) or unavailable (0). Unlike mutexes, which are designed for mutual exclusion and have ownership semantics, binary semaphores are primarily used for signaling between tasks or threads. A common pattern is one task waiting for an event while another task signals that the event has occurred.

The key distinction from mutexes is that the task that "gives" (signals) a binary semaphore doesn't need to be the same task that "takes" (waits on) it. This makes them ideal for producer-consumer scenarios, interrupt-to-task communication, and event notifications.

## Linux Implementation

Linux provides two main semaphore APIs: POSIX semaphores (more modern and portable) and System V semaphores (older, more complex).

### POSIX Semaphores Example

POSIX semaphores come in two flavors: named (for inter-process communication) and unnamed (for threads within the same process). Here's an example of using an unnamed semaphore for thread synchronization:

```c
#include <pthread.h>
#include <semaphore.h>
#include <stdio.h>
#include <unistd.h>

sem_t binary_sem;

void* worker_thread(void* arg) {
    printf("Worker: Waiting for signal...\n");
    sem_wait(&binary_sem);  // Wait for signal
    printf("Worker: Received signal, processing data\n");
    // Process the data here
    return NULL;
}

void* producer_thread(void* arg) {
    sleep(2);  // Simulate some work
    printf("Producer: Preparing data and signaling worker\n");
    sem_post(&binary_sem);  // Signal the worker
    return NULL;
}

int main() {
    pthread_t worker, producer;
    
    // Initialize binary semaphore with value 0 (unavailable)
    sem_init(&binary_sem, 0, 0);
    
    pthread_create(&worker, NULL, worker_thread, NULL);
    pthread_create(&producer, NULL, producer_thread, NULL);
    
    pthread_join(worker, NULL);
    pthread_join(producer, NULL);
    
    sem_destroy(&binary_sem);
    return 0;
}
```

### System V Semaphores Example

System V semaphores are more heavyweight and designed for inter-process communication. They operate on semaphore sets:

```c
#include <sys/sem.h>
#include <sys/types.h>
#include <sys/ipc.h>
#include <stdio.h>

union semun {
    int val;
    struct semid_ds *buf;
    unsigned short *array;
};

int main() {
    key_t key = ftok("/tmp", 'S');
    int semid = semget(key, 1, IPC_CREAT | 0666);
    
    // Initialize to 0 (unavailable)
    union semun arg;
    arg.val = 0;
    semctl(semid, 0, SETVAL, arg);
    
    if (fork() == 0) {
        // Child process: wait for signal
        struct sembuf wait_op = {0, -1, 0};  // Decrement (wait)
        printf("Child: Waiting for signal\n");
        semop(semid, &wait_op, 1);
        printf("Child: Signal received!\n");
    } else {
        // Parent process: send signal
        sleep(2);
        struct sembuf signal_op = {0, 1, 0};  // Increment (signal)
        printf("Parent: Sending signal\n");
        semop(semid, &signal_op, 1);
        wait(NULL);
        semctl(semid, 0, IPC_RMID);  // Clean up
    }
    
    return 0;
}
```

## FreeRTOS Implementation

FreeRTOS provides straightforward binary semaphore support through its API. Binary semaphores are often used for synchronizing tasks with interrupt service routines (ISRs).

### Basic Task-to-Task Signaling

```c
#include "FreeRTOS.h"
#include "task.h"
#include "semphr.h"

SemaphoreHandle_t xBinarySemaphore;

void vSenderTask(void *pvParameters) {
    while(1) {
        vTaskDelay(pdMS_TO_TICKS(1000));
        
        // Signal the receiver task
        xSemaphoreGive(xBinarySemaphore);
        printf("Sender: Signal sent\n");
    }
}

void vReceiverTask(void *pvParameters) {
    while(1) {
        // Wait indefinitely for signal
        if(xSemaphoreTake(xBinarySemaphore, portMAX_DELAY) == pdTRUE) {
            printf("Receiver: Signal received, processing event\n");
            // Handle the event
        }
    }
}

int main(void) {
    // Create binary semaphore (initially unavailable)
    xBinarySemaphore = xSemaphoreCreateBinary();
    
    if(xBinarySemaphore != NULL) {
        xTaskCreate(vReceiverTask, "Receiver", 1000, NULL, 2, NULL);
        xTaskCreate(vSenderTask, "Sender", 1000, NULL, 1, NULL);
        vTaskStartScheduler();
    }
    
    return 0;
}
```

### ISR-to-Task Signaling (Common Use Case)

A particularly important use case in embedded systems is signaling from an interrupt to a task:

```c
SemaphoreHandle_t xButtonSemaphore;

// Interrupt service routine
void EXTI0_IRQHandler(void) {
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;
    
    // Clear interrupt flag
    EXTI_ClearITPendingBit(EXTI_Line0);
    
    // Signal the task from ISR
    xSemaphoreGiveFromISR(xButtonSemaphore, &xHigherPriorityTaskWoken);
    
    // Request context switch if needed
    portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
}

void vButtonHandlerTask(void *pvParameters) {
    while(1) {
        // Wait for button press interrupt
        if(xSemaphoreTake(xButtonSemaphore, portMAX_DELAY) == pdTRUE) {
            printf("Button pressed! Handling in task context\n");
            // Perform time-consuming button handling here
        }
    }
}

void setup(void) {
    xButtonSemaphore = xSemaphoreCreateBinary();
    xTaskCreate(vButtonHandlerTask, "ButtonHandler", 1000, NULL, 3, NULL);
    // Configure button interrupt...
}
```

## RTIC (Real-Time Interrupt-driven Concurrency)

RTIC is a Rust framework for ARM Cortex-M microcontrollers that takes a different approach. Instead of traditional semaphores, RTIC uses software tasks and message passing. However, you can achieve similar signaling behavior using software tasks that are spawned by hardware tasks (interrupts) or other software tasks.

### RTIC Example with Software Task Signaling

```rust
#![no_main]
#![no_std]

use panic_halt as _;

#[rtic::app(device = stm32f4xx_hal::pac, dispatchers = [EXTI0, EXTI1])]
mod app {
    use stm32f4xx_hal::prelude::*;
    
    #[shared]
    struct Shared {}

    #[local]
    struct Local {
        button: stm32f4xx_hal::gpio::gpioa::PA0<Input>,
    }

    #[init]
    fn init(ctx: init::Context) -> (Shared, Local, init::Monotonics) {
        let dp = ctx.device;
        let gpioa = dp.GPIOA.split();
        let button = gpioa.pa0.into_pull_down_input();
        
        (
            Shared {},
            Local { button },
            init::Monotonics(),
        )
    }

    // Hardware task (interrupt-driven)
    #[task(binds = EXTI0, local = [button])]
    fn button_handler(ctx: button_handler::Context) {
        // Clear interrupt
        // ctx.local.button.clear_interrupt_pending_bit();
        
        // Spawn software task to handle the event
        process_button::spawn().ok();
    }

    // Software task (signaled by hardware task)
    #[task]
    fn process_button(_ctx: process_button::Context) {
        // This task runs when spawned, similar to taking a semaphore
        // Perform button processing here
        defmt::info!("Button event processed in software task");
    }
    
    // Another example: periodic task signaling a worker
    #[task]
    fn periodic_producer(_ctx: periodic_producer::Context) {
        // Do some work
        
        // Signal the worker task
        worker::spawn().ok();
    }
    
    #[task]
    fn worker(_ctx: worker::Context) {
        defmt::info!("Worker task signaled and running");
        // Process the work
    }
}
```

For more traditional semaphore-like behavior in RTIC, you might use a capacity-constrained channel or an atomic flag:

```rust
use core::sync::atomic::{AtomicBool, Ordering};

#[shared]
struct Shared {
    event_flag: AtomicBool,
}

#[task(shared = [event_flag])]
fn producer(mut ctx: producer::Context) {
    // Signal the event
    ctx.shared.event_flag.lock(|flag| {
        flag.store(true, Ordering::Release);
    });
    
    consumer::spawn().ok();
}

#[task(shared = [event_flag])]
fn consumer(mut ctx: consumer::Context) {
    ctx.shared.event_flag.lock(|flag| {
        if flag.load(Ordering::Acquire) {
            flag.store(false, Ordering::Release);
            // Process the event
            defmt::info!("Event consumed");
        }
    });
}
```

## Embassy Framework

Embassy is an async Rust framework for embedded systems that provides modern concurrency primitives. For binary semaphore-like signaling, Embassy offers the `Signal` type, which is specifically designed for single-producer, single-consumer signaling.

### Embassy Signal Example

```rust
#![no_std]
#![no_main]

use embassy_executor::Spawner;
use embassy_sync::signal::Signal;
use embassy_sync::blocking_mutex::raw::ThreadModeRawMutex;
use embassy_time::{Duration, Timer};

// Create a signal for task-to-task communication
static EVENT_SIGNAL: Signal<ThreadModeRawMutex, ()> = Signal::new();

#[embassy_executor::task]
async fn producer_task() {
    loop {
        Timer::after(Duration::from_secs(2)).await;
        
        // Signal the event
        defmt::info!("Producer: Signaling event");
        EVENT_SIGNAL.signal(());
    }
}

#[embassy_executor::task]
async fn consumer_task() {
    loop {
        // Wait for the signal
        EVENT_SIGNAL.wait().await;
        defmt::info!("Consumer: Received signal, processing");
        
        // Process the event
        // The signal is automatically reset after wait() returns
    }
}

#[embassy_executor::main]
async fn main(spawner: Spawner) {
    spawner.spawn(producer_task()).unwrap();
    spawner.spawn(consumer_task()).unwrap();
}
```

### Embassy Signal with Data

Unlike basic binary semaphores, Embassy's `Signal` can carry data:

```rust
use embassy_sync::signal::Signal;
use embassy_sync::blocking_mutex::raw::ThreadModeRawMutex;

#[derive(Clone, Copy)]
struct SensorData {
    temperature: i16,
    humidity: u8,
}

static SENSOR_SIGNAL: Signal<ThreadModeRawMutex, SensorData> = Signal::new();

#[embassy_executor::task]
async fn sensor_reader() {
    loop {
        Timer::after(Duration::from_millis(1000)).await;
        
        let data = SensorData {
            temperature: 25,
            humidity: 60,
        };
        
        SENSOR_SIGNAL.signal(data);
    }
}

#[embassy_executor::task]
async fn data_processor() {
    loop {
        let data = SENSOR_SIGNAL.wait().await;
        defmt::info!(
            "Received sensor data: temp={}°C, humidity={}%",
            data.temperature,
            data.humidity
        );
    }
}
```

### Embassy Channel for Multiple Signaling

For scenarios where you need multiple signals queued (similar to counting semaphores), Embassy provides `Channel`:

```rust
use embassy_sync::channel::Channel;
use embassy_sync::blocking_mutex::raw::ThreadModeRawMutex;

static EVENT_CHANNEL: Channel<ThreadModeRawMutex, (), 5> = Channel::new();

#[embassy_executor::task]
async fn interrupt_handler() {
    loop {
        // Wait for actual interrupt via some mechanism
        
        // Send signal (non-blocking if queue not full)
        EVENT_CHANNEL.send(()).await;
    }
}

#[embassy_executor::task]
async fn event_processor() {
    loop {
        EVENT_CHANNEL.receive().await;
        defmt::info!("Processing queued event");
        // Handle the event
    }
}
```

## Key Differences Summary

The approach to binary semaphore-style signaling varies significantly across these systems. Linux provides explicit POSIX and System V semaphore APIs for thread and process synchronization. FreeRTOS offers traditional binary semaphore objects with dedicated create, give, and take functions that closely mirror classical RTOS semantics. RTIC leverages Rust's type system and uses task spawning as its primary signaling mechanism, avoiding explicit semaphore objects. Embassy embraces async/await patterns and provides the `Signal` primitive that naturally integrates with Rust's async ecosystem.

The choice of which to use depends on your constraints: Linux semaphores for desktop/server applications with mature POSIX support, FreeRTOS for resource-constrained embedded systems with C codebases, RTIC for zero-cost abstractions in Rust on Cortex-M, and Embassy for modern async embedded development in Rust.