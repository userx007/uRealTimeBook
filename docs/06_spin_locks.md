# Spinlocks and critical sections 

**Linux** provides true spinlocks with busy-waiting, ideal for multiprocessor systems. The `spin_lock_irqsave` variant is crucial when shared data can be accessed from interrupt handlers, preventing deadlocks by disabling interrupts while holding the lock.

**FreeRTOS** uses critical sections (`taskENTER_CRITICAL`/`taskEXIT_CRITICAL`) that disable interrupts rather than spinning. This works well on single-core microcontrollers where there's no other core to yield to. It's simple but can increase interrupt latency.

**RTIC** takes a more sophisticated approach with priority-based preemption and automatic lock management through its resource model. The framework uses the priority ceiling protocol, which is provably deadlock-free and optimal for hard real-time systems.

**Embassy** provides async-aware primitives like `CriticalSectionMutex` that work seamlessly with its async runtime. This avoids blocking while still providing safe concurrent access through cooperative multitasking.

# Spinlocks and Critical Sections Across Operating Systems

## Overview

**Spinlocks** are synchronization primitives that use busy-waiting (spinning) to acquire a lock. Unlike blocking locks that put a thread to sleep, spinlocks continuously check if the lock is available in a tight loop. They're ideal for protecting very short critical sections where the overhead of context switching would exceed the spin time.

### Key Characteristics

- **Busy-waiting**: CPU actively polls the lock instead of sleeping
- **Short critical sections**: Best for operations measured in nanoseconds/microseconds
- **No context switch**: Avoids scheduler overhead
- **CPU waste**: Inefficient for long waits
- **Interrupt considerations**: Often need to disable interrupts to prevent deadlocks

---

## 1. Linux Kernel Spinlocks

Linux provides several spinlock variants for different scenarios in kernel space.

### Basic Spinlock

```c
#include <linux/spinlock.h>

static DEFINE_SPINLOCK(my_lock);
static int shared_counter = 0;

void increment_counter(void)
{
    spin_lock(&my_lock);
    shared_counter++;  // Critical section
    spin_unlock(&my_lock);
}
```

### Spinlock with IRQ Disabling

When code can be interrupted by IRQ handlers that might also try to acquire the same lock:

```c
static DEFINE_SPINLOCK(device_lock);
static struct device_data dev_data;

void update_device_data(int new_value)
{
    unsigned long flags;
    
    // Save IRQ state and disable interrupts
    spin_lock_irqsave(&device_lock, flags);
    
    dev_data.value = new_value;
    dev_data.timestamp = jiffies;
    
    // Restore previous IRQ state
    spin_unlock_irqrestore(&device_lock, flags);
}

// In IRQ handler
irqreturn_t device_irq_handler(int irq, void *dev_id)
{
    unsigned long flags;
    
    spin_lock_irqsave(&device_lock, flags);
    // Access shared data safely
    int val = dev_data.value;
    spin_unlock_irqrestore(&device_lock, flags);
    
    return IRQ_HANDLED;
}
```

### Bottom Half Spinlock

For protecting data shared with softirqs/tasklets:

```c
static DEFINE_SPINLOCK(bh_lock);

void process_data(void)
{
    spin_lock_bh(&bh_lock);  // Disables bottom halves
    // Critical section
    spin_unlock_bh(&bh_lock);
}
```

### Read-Write Spinlocks

Allow multiple readers but exclusive writer access:

```c
static DEFINE_RWLOCK(rw_lock);
static struct shared_config config;

void read_config(struct user_config *out)
{
    read_lock(&rw_lock);
    memcpy(out, &config, sizeof(config));
    read_unlock(&rw_lock);
}

void write_config(const struct user_config *in)
{
    write_lock(&rw_lock);
    memcpy(&config, in, sizeof(config));
    write_unlock(&rw_lock);
}
```

### Practical Example: Ring Buffer

```c
#include <linux/spinlock.h>
#include <linux/module.h>

#define BUFFER_SIZE 256

struct ring_buffer {
    spinlock_t lock;
    char data[BUFFER_SIZE];
    unsigned int head;
    unsigned int tail;
};

static struct ring_buffer my_buffer;

int buffer_write(char byte)
{
    unsigned long flags;
    unsigned int next_head;
    
    spin_lock_irqsave(&my_buffer.lock, flags);
    
    next_head = (my_buffer.head + 1) % BUFFER_SIZE;
    if (next_head == my_buffer.tail) {
        // Buffer full
        spin_unlock_irqrestore(&my_buffer.lock, flags);
        return -ENOMEM;
    }
    
    my_buffer.data[my_buffer.head] = byte;
    my_buffer.head = next_head;
    
    spin_unlock_irqrestore(&my_buffer.lock, flags);
    return 0;
}
```

---

## 2. FreeRTOS Critical Sections

FreeRTOS doesn't have traditional spinlocks but uses critical sections that disable interrupts or the scheduler.

### Basic Critical Section

```c
#include "FreeRTOS.h"
#include "task.h"

static volatile uint32_t shared_resource = 0;

void modify_resource(void)
{
    // Enter critical section - disables interrupts up to configMAX_SYSCALL_INTERRUPT_PRIORITY
    taskENTER_CRITICAL();
    
    shared_resource++;
    shared_resource *= 2;
    
    // Exit critical section - re-enables interrupts
    taskEXIT_CRITICAL();
}
```

### Critical Section from ISR

For interrupt service routines:

```c
static volatile uint32_t isr_counter = 0;

void vInterruptHandler(void)
{
    UBaseType_t saved_interrupt_status;
    
    // Save interrupt status and enter critical section
    saved_interrupt_status = taskENTER_CRITICAL_FROM_ISR();
    
    isr_counter++;
    // Access shared data
    
    // Restore interrupt status
    taskEXIT_CRITICAL_FROM_ISR(saved_interrupt_status);
}
```

### Scheduler Suspension (Alternative)

For task-level synchronization without disabling interrupts:

```c
void modify_data_structure(void)
{
    // Suspend scheduler - tasks won't switch but interrupts still run
    vTaskSuspendAll();
    
    // Modify complex data structure
    // This is safe from other tasks but NOT from ISRs
    
    // Resume scheduler
    xTaskResumeAll();
}
```

### Practical Example: Shared Queue Access

```c
#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"

static QueueHandle_t data_queue;
static volatile uint32_t queue_stats[2] = {0}; // [writes, reads]

void producer_task(void *params)
{
    uint32_t data = 0;
    
    while (1) {
        data++;
        
        if (xQueueSend(data_queue, &data, portMAX_DELAY) == pdPASS) {
            // Update statistics atomically
            taskENTER_CRITICAL();
            queue_stats[0]++;
            taskEXIT_CRITICAL();
        }
        
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

void consumer_task(void *params)
{
    uint32_t received_data;
    
    while (1) {
        if (xQueueReceive(data_queue, &received_data, portMAX_DELAY) == pdPASS) {
            // Update statistics atomically
            taskENTER_CRITICAL();
            queue_stats[1]++;
            taskEXIT_CRITICAL();
            
            // Process data
        }
    }
}

// Timer ISR that logs statistics
void vTimerISRHandler(void)
{
    UBaseType_t saved_status;
    uint32_t writes, reads;
    
    saved_status = taskENTER_CRITICAL_FROM_ISR();
    writes = queue_stats[0];
    reads = queue_stats[1];
    taskEXIT_CRITICAL_FROM_ISR(saved_status);
    
    // Log statistics
}
```

---

## 3. RTIC (Real-Time Interrupt-driven Concurrency)

RTIC uses priority-based interrupt masking for critical sections. The framework automatically manages critical sections through its resource model.

### Automatic Resource Locking

```rust
#[rtic::app(device = stm32f4::stm32f401, peripherals = true)]
mod app {
    use rtic::Mutex;
    
    #[shared]
    struct Shared {
        counter: u32,
        buffer: [u8; 64],
    }
    
    #[local]
    struct Local {}
    
    #[init]
    fn init(cx: init::Context) -> (Shared, Local) {
        (
            Shared {
                counter: 0,
                buffer: [0; 64],
            },
            Local {},
        )
    }
    
    // Low priority task
    #[task(shared = [counter], priority = 1)]
    async fn low_priority(mut cx: low_priority::Context) {
        // Automatically locks 'counter' with critical section
        cx.shared.counter.lock(|counter| {
            *counter += 1;
        });
    }
    
    // High priority task - can preempt low_priority
    #[task(shared = [counter], priority = 2)]
    async fn high_priority(mut cx: high_priority::Context) {
        // Also locks 'counter' - priority ceiling protocol prevents deadlocks
        cx.shared.counter.lock(|counter| {
            *counter *= 2;
        });
    }
}
```

### Manual Critical Sections

```rust
use cortex_m::interrupt;

static mut SHARED_DATA: u32 = 0;

pub fn update_shared_data() {
    // Disable all interrupts
    interrupt::free(|_cs| {
        unsafe {
            SHARED_DATA += 1;
        }
    });
}
```

### Priority-Based Critical Sections

```rust
#[rtic::app(device = stm32f4::stm32f401)]
mod app {
    use rtic::Mutex;
    
    #[shared]
    struct Shared {
        sensor_data: SensorData,
        timestamp: u64,
    }
    
    #[local]
    struct Local {}
    
    // Sensor interrupt - priority 2
    #[task(binds = EXTI0, shared = [sensor_data, timestamp], priority = 2)]
    fn sensor_interrupt(mut cx: sensor_interrupt::Context) {
        let now = get_timestamp();
        let data = read_sensor();
        
        // Lock both resources atomically
        (cx.shared.sensor_data, cx.shared.timestamp).lock(|sensor_data, timestamp| {
            *sensor_data = data;
            *timestamp = now;
        });
    }
    
    // Processing task - priority 1
    #[task(shared = [sensor_data, timestamp], priority = 1)]
    async fn process_data(mut cx: process_data::Context) {
        let (data, time) = (cx.shared.sensor_data, cx.shared.timestamp).lock(
            |sensor_data, timestamp| {
                (*sensor_data, *timestamp)
            }
        );
        
        // Process the copied data
    }
}
```

### Practical Example: ADC with DMA

```rust
#[rtic::app(device = stm32f4::stm32f401, peripherals = true, dispatchers = [EXTI1])]
mod app {
    use rtic::Mutex;
    use stm32f4xx_hal::{adc::Adc, prelude::*};
    
    #[shared]
    struct Shared {
        adc_buffer: [u16; 128],
        buffer_ready: bool,
    }
    
    #[local]
    struct Local {
        adc: Adc<ADC1>,
    }
    
    #[init]
    fn init(cx: init::Context) -> (Shared, Local) {
        // Setup hardware
        let dp = cx.device;
        
        (
            Shared {
                adc_buffer: [0; 128],
                buffer_ready: false,
            },
            Local {
                adc: setup_adc(dp.ADC1),
            },
        )
    }
    
    // DMA interrupt - priority 3 (highest)
    #[task(binds = DMA2_STREAM0, shared = [adc_buffer, buffer_ready], priority = 3)]
    fn dma_complete(mut cx: dma_complete::Context) {
        // Critical section automatically created by RTIC
        (cx.shared.adc_buffer, cx.shared.buffer_ready).lock(
            |buffer, ready| {
                // Buffer filled by DMA, mark as ready
                *ready = true;
            }
        );
        
        process_data::spawn().ok();
    }
    
    // Processing task - priority 1 (lower)
    #[task(shared = [adc_buffer, buffer_ready], priority = 1)]
    async fn process_data(mut cx: process_data::Context) {
        let mut local_buffer = [0u16; 128];
        let has_data = (cx.shared.adc_buffer, cx.shared.buffer_ready).lock(
            |buffer, ready| {
                if *ready {
                    local_buffer.copy_from_slice(buffer);
                    *ready = false;
                    true
                } else {
                    false
                }
            }
        );
        
        if has_data {
            // Process local copy without holding lock
            let average = local_buffer.iter().sum::<u16>() / local_buffer.len() as u16;
        }
    }
}
```

---

## 4. Embassy

Embassy provides async-aware synchronization primitives including critical section mutexes.

### CriticalSectionMutex

```rust
use embassy_sync::blocking_mutex::raw::CriticalSectionRawMutex;
use embassy_sync::mutex::Mutex;

static SHARED_COUNTER: Mutex<CriticalSectionRawMutex, u32> = Mutex::new(0);

#[embassy_executor::task]
async fn task1() {
    loop {
        {
            let mut counter = SHARED_COUNTER.lock().await;
            *counter += 1;
        } // Lock automatically released
        
        Timer::after(Duration::from_millis(100)).await;
    }
}

#[embassy_executor::task]
async fn task2() {
    loop {
        {
            let counter = SHARED_COUNTER.lock().await;
            defmt::info!("Counter: {}", *counter);
        }
        
        Timer::after(Duration::from_millis(500)).await;
    }
}
```

### Blocking Mutex for Interrupt Context

```rust
use embassy_sync::blocking_mutex::{raw::CriticalSectionRawMutex, Mutex as BlockingMutex};

static ISR_SHARED: BlockingMutex<CriticalSectionRawMutex, u32> = BlockingMutex::new(0);

// Can be used from interrupt context
#[interrupt]
fn UART0() {
    ISR_SHARED.lock(|data| {
        *data += 1;
    });
}

// Also from async tasks
#[embassy_executor::task]
async fn monitor() {
    loop {
        let value = ISR_SHARED.lock(|data| *data);
        defmt::info!("ISR count: {}", value);
        Timer::after(Duration::from_secs(1)).await;
    }
}
```

### Channel with Critical Section Protection

```rust
use embassy_sync::channel::Channel;
use embassy_sync::blocking_mutex::raw::CriticalSectionRawMutex;

static SENSOR_CHANNEL: Channel<CriticalSectionRawMutex, SensorReading, 10> = Channel::new();

#[embassy_executor::task]
async fn sensor_reader() {
    loop {
        let reading = read_sensor().await;
        SENSOR_CHANNEL.send(reading).await;
    }
}

#[embassy_executor::task]
async fn sensor_processor() {
    loop {
        let reading = SENSOR_CHANNEL.receive().await;
        process_reading(reading).await;
    }
}
```

### Practical Example: Multi-Sensor System

```rust
use embassy_executor::Spawner;
use embassy_sync::blocking_mutex::raw::CriticalSectionRawMutex;
use embassy_sync::mutex::Mutex;
use embassy_time::{Duration, Timer};

#[derive(Clone, Copy)]
struct SensorData {
    temperature: f32,
    humidity: f32,
    pressure: f32,
    timestamp_ms: u64,
}

impl Default for SensorData {
    fn default() -> Self {
        Self {
            temperature: 0.0,
            humidity: 0.0,
            pressure: 0.0,
            timestamp_ms: 0,
        }
    }
}

static SENSOR_DATA: Mutex<CriticalSectionRawMutex, SensorData> = 
    Mutex::new(SensorData::default());

#[embassy_executor::task]
async fn temperature_task(i2c: /* I2C peripheral */) {
    loop {
        let temp = read_temperature_sensor(&i2c).await;
        let now = embassy_time::Instant::now().as_millis();
        
        {
            let mut data = SENSOR_DATA.lock().await;
            data.temperature = temp;
            data.timestamp_ms = now;
        }
        
        Timer::after(Duration::from_millis(1000)).await;
    }
}

#[embassy_executor::task]
async fn humidity_task(i2c: /* I2C peripheral */) {
    loop {
        let humidity = read_humidity_sensor(&i2c).await;
        
        {
            let mut data = SENSOR_DATA.lock().await;
            data.humidity = humidity;
        }
        
        Timer::after(Duration::from_millis(2000)).await;
    }
}

#[embassy_executor::task]
async fn pressure_task(spi: /* SPI peripheral */) {
    loop {
        let pressure = read_pressure_sensor(&spi).await;
        
        {
            let mut data = SENSOR_DATA.lock().await;
            data.pressure = pressure;
        }
        
        Timer::after(Duration::from_millis(500)).await;
    }
}

#[embassy_executor::task]
async fn display_task() {
    loop {
        let snapshot = {
            let data = SENSOR_DATA.lock().await;
            *data  // Copy the data
        };
        
        defmt::info!(
            "Temp: {:.1}°C, Humidity: {:.1}%, Pressure: {:.1}hPa (@ {}ms)",
            snapshot.temperature,
            snapshot.humidity,
            snapshot.pressure,
            snapshot.timestamp_ms
        );
        
        Timer::after(Duration::from_millis(5000)).await;
    }
}

#[embassy_executor::main]
async fn main(spawner: Spawner) {
    // Initialize hardware
    
    spawner.spawn(temperature_task(/* i2c */)).unwrap();
    spawner.spawn(humidity_task(/* i2c */)).unwrap();
    spawner.spawn(pressure_task(/* spi */)).unwrap();
    spawner.spawn(display_task()).unwrap();
}
```

---

## Comparison Summary

| Feature | Linux | FreeRTOS | RTIC | Embassy |
|---------|-------|----------|------|---------|
| **Mechanism** | True spinlock | Interrupt disable | Priority ceiling | Critical section mutex |
| **Busy-wait** | Yes | No | No | No (async) |
| **Context** | Kernel space | Embedded (no MMU) | Embedded (Cortex-M) | Embedded (async) |
| **Overhead** | Low | Very low | Very low | Low |
| **Deadlock Protection** | Manual | Manual | Automatic (priority) | Automatic (async) |
| **IRQ Safety** | spin_lock_irqsave | taskENTER_CRITICAL_FROM_ISR | Built-in | BlockingMutex |
| **Best For** | SMP systems, short locks | Simple embedded | Hard real-time | Async embedded |

## When to Use Spinlocks vs. Alternatives

**Use spinlocks/critical sections when:**
- Critical section is very short (< 100 microseconds)
- Running on multiprocessor systems (Linux)
- Must protect against interrupts
- Context switch overhead is too high
- Lock contention is very low

**Avoid spinlocks when:**
- Critical section is long
- Lock is frequently contended
- Operations inside can block/sleep
- On single-core systems where blocking locks suffice
- Holding the lock while doing I/O or memory allocation