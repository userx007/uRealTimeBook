# Embassy-Specific Async Mutex/RwLock

## Overview

Embassy provides **async-aware synchronization primitives** specifically designed for embedded async/await programming in Rust. Unlike standard blocking locks, Embassy's `Mutex` and `RwLock` are cooperative - they **yield control back to the executor** when contention occurs, allowing other tasks to run instead of spinning or blocking the entire system.

## Core Concepts

### Why Async Locks?

In embedded systems with Embassy's executor:
- **No standard library threading** - embedded targets often lack OS-level threads
- **Cooperative multitasking** - tasks must yield voluntarily
- **Resource efficiency** - blocking wastes precious CPU cycles
- **Deadlock prevention** - async locks integrate with the executor's scheduling

### Key Differences from Standard Locks

| Standard `std::sync::Mutex` | Embassy `embassy_sync::mutex::Mutex` |
|----------------------------|-------------------------------------|
| Blocks thread execution | Yields to executor |
| Requires OS threads | Works with single-threaded async |
| Spin-waits or sleeps | Cooperatively waits |
| Can't be used in async | `.lock().await` syntax |

## Embassy Mutex

### Basic Structure

```rust
use embassy_sync::mutex::Mutex;
use embassy_sync::blocking_mutex::raw::CriticalSectionRawMutex;

// Mutex requires a "RawMutex" implementation for critical sections
static SHARED_COUNTER: Mutex<CriticalSectionRawMutex, u32> = 
    Mutex::new(0);
```

### Complete Example: Shared Resource Access

```rust
use embassy_executor::Spawner;
use embassy_sync::mutex::Mutex;
use embassy_sync::blocking_mutex::raw::CriticalSectionRawMutex;
use embassy_time::{Duration, Timer};

// Shared buffer protected by async mutex
static SHARED_BUFFER: Mutex<CriticalSectionRawMutex, [u8; 64]> = 
    Mutex::new([0; 64]);

#[embassy_executor::task]
async fn writer_task(id: u8) {
    loop {
        // Acquire lock asynchronously
        let mut buffer = SHARED_BUFFER.lock().await;
        
        // Critical section - we have exclusive access
        buffer[0] = id;
        buffer[1] = buffer[1].wrapping_add(1);
        
        log::info!("Writer {} updated buffer: {:?}", id, &buffer[..8]);
        
        // Lock is automatically released when `buffer` goes out of scope
        drop(buffer);
        
        Timer::after(Duration::from_millis(100)).await;
    }
}

#[embassy_executor::task]
async fn reader_task() {
    loop {
        let buffer = SHARED_BUFFER.lock().await;
        log::info!("Reader sees: writer={}, count={}", 
                   buffer[0], buffer[1]);
        drop(buffer);
        
        Timer::after(Duration::from_millis(50)).await;
    }
}

#[embassy_executor::main]
async fn main(spawner: Spawner) {
    spawner.spawn(writer_task(1)).unwrap();
    spawner.spawn(writer_task(2)).unwrap();
    spawner.spawn(reader_task()).unwrap();
}
```

### How It Works

1. **Lock Acquisition**: When `.lock().await` is called:
   - If available: immediately returns guard
   - If locked: task **yields** to executor, goes into wait queue
   
2. **Executor Integration**: 
   - Waiting task is suspended (not spinning)
   - Other tasks continue running
   - When lock is released, waiting task is **woken** and scheduled

3. **RAII Guard**: The returned `MutexGuard` automatically releases the lock when dropped

## Embassy RwLock

### Structure

`RwLock` allows **multiple concurrent readers** or **one exclusive writer**:

```rust
use embassy_sync::mutex::Mutex;
use embassy_sync::blocking_mutex::raw::CriticalSectionRawMutex;

// Note: As of current Embassy versions, RwLock usage is similar
// This example shows the pattern for read-heavy scenarios
```

### Practical Example: Sensor Data Cache

```rust
use embassy_sync::mutex::Mutex;
use embassy_sync::blocking_mutex::raw::CriticalSectionRawMutex;

#[derive(Clone, Copy)]
struct SensorData {
    temperature: f32,
    humidity: f32,
    timestamp: u64,
}

static SENSOR_CACHE: Mutex<CriticalSectionRawMutex, SensorData> = 
    Mutex::new(SensorData {
        temperature: 0.0,
        humidity: 0.0,
        timestamp: 0,
    });

// Writer: Updates sensor data periodically
#[embassy_executor::task]
async fn sensor_updater() {
    loop {
        // Read from actual sensor (blocking I/O)
        let new_data = read_sensor_hardware().await;
        
        // Update shared cache
        let mut cache = SENSOR_CACHE.lock().await;
        *cache = new_data;
        drop(cache);
        
        Timer::after(Duration::from_secs(1)).await;
    }
}

// Multiple readers: Display, logging, network
#[embassy_executor::task]
async fn display_task() {
    loop {
        let data = SENSOR_CACHE.lock().await;
        update_display(data.temperature, data.humidity);
        drop(data);
        
        Timer::after(Duration::from_millis(100)).await;
    }
}

#[embassy_executor::task]
async fn network_reporter() {
    loop {
        let data = SENSOR_CACHE.lock().await;
        send_to_server(*data).await;
        drop(data);
        
        Timer::after(Duration::from_secs(60)).await;
    }
}
```

## Raw Mutex Types

Embassy requires specifying a **raw mutex** implementation for the underlying critical section:

### 1. **CriticalSectionRawMutex**
```rust
use embassy_sync::blocking_mutex::raw::CriticalSectionRawMutex;

// Uses cortex_m critical sections (disables interrupts)
static DATA: Mutex<CriticalSectionRawMutex, SharedData> = Mutex::new(...);
```
- **Use case**: General purpose, works across interrupt contexts
- **Safety**: Disables interrupts briefly during lock/unlock

### 2. **ThreadModeRawMutex**
```rust
use embassy_sync::blocking_mutex::raw::ThreadModeRawMutex;

// Lighter weight, only for thread mode (not in ISRs)
static DATA: Mutex<ThreadModeRawMutex, SharedData> = Mutex::new(...);
```
- **Use case**: Task-to-task synchronization only
- **Limitation**: Cannot be used in interrupt handlers

### 3. **NoopRawMutex**
```rust
use embassy_sync::blocking_mutex::raw::NoopRawMutex;

// No actual locking (single-threaded, no interrupts)
static DATA: Mutex<NoopRawMutex, SharedData> = Mutex::new(...);
```
- **Use case**: Single-task scenarios or testing
- **Warning**: Unsafe if multiple tasks access concurrently

## Advanced Pattern: Try-Lock

```rust
use embassy_sync::mutex::Mutex;
use embassy_time::{with_timeout, Duration};

#[embassy_executor::task]
async fn opportunistic_writer() {
    loop {
        // Try to acquire lock with timeout
        match with_timeout(
            Duration::from_millis(10),
            SHARED_BUFFER.lock()
        ).await {
            Ok(mut buffer) => {
                // Got the lock - do work
                buffer[0] = 42;
                log::info!("Updated buffer");
            }
            Err(_) => {
                // Couldn't acquire in time - skip this cycle
                log::warn!("Lock busy, skipping update");
            }
        }
        
        Timer::after(Duration::from_millis(50)).await;
    }
}
```

## Real-World Use Case: UART Buffer Management

```rust
use embassy_sync::mutex::Mutex;
use embassy_sync::blocking_mutex::raw::CriticalSectionRawMutex;
use heapless::Vec;

const BUFFER_SIZE: usize = 256;

struct UartBuffer {
    data: Vec<u8, BUFFER_SIZE>,
    overflow_count: u32,
}

static UART_RX_BUFFER: Mutex<CriticalSectionRawMutex, UartBuffer> = 
    Mutex::new(UartBuffer {
        data: Vec::new(),
        overflow_count: 0,
    });

// ISR writes to buffer
#[interrupt]
fn UART_IRQ() {
    if let Some(byte) = read_uart_byte() {
        // Try to acquire lock (non-blocking in ISR context)
        if let Ok(mut buffer) = UART_RX_BUFFER.try_lock() {
            if buffer.data.push(byte).is_err() {
                buffer.overflow_count += 1;
            }
        }
    }
}

// Task processes buffer
#[embassy_executor::task]
async fn uart_processor() {
    loop {
        let mut buffer = UART_RX_BUFFER.lock().await;
        
        if !buffer.data.is_empty() {
            process_data(&buffer.data);
            buffer.data.clear();
        }
        
        if buffer.overflow_count > 0 {
            log::warn!("Buffer overflows: {}", buffer.overflow_count);
            buffer.overflow_count = 0;
        }
        
        drop(buffer);
        Timer::after(Duration::from_millis(10)).await;
    }
}
```

## Best Practices

1. **Keep Critical Sections Short**
   ```rust
   // BAD: Holding lock during long operation
   let mut data = MUTEX.lock().await;
   expensive_computation(&data);
   
   // GOOD: Release lock quickly
   let snapshot = {
       let data = MUTEX.lock().await;
       data.clone()
   };
   expensive_computation(&snapshot);
   ```

2. **Avoid Nested Locks** (can cause deadlocks)
   ```rust
   // RISKY: Nested lock acquisition
   let a = MUTEX_A.lock().await;
   let b = MUTEX_B.lock().await;  // Could deadlock
   ```

3. **Use Explicit Drop**
   ```rust
   let guard = MUTEX.lock().await;
   // ... use guard ...
   drop(guard);  // Explicit release point
   // Other async operations here
   ```

4. **Choose Appropriate Raw Mutex**
   - ISR access required → `CriticalSectionRawMutex`
   - Task-only access → `ThreadModeRawMutex`
   - Single task → `NoopRawMutex`

## Performance Characteristics

| Operation | Embassy Async | Blocking Mutex |
|-----------|---------------|----------------|
| Uncontended lock | ~10-50 cycles | ~5-20 cycles |
| Contended lock | Yields (0 spin) | Spins/blocks |
| Memory overhead | ~4-8 bytes | ~4 bytes |
| Fairness | FIFO wake queue | Depends on OS |

## Summary

Embassy's async Mutex/RwLock provide **cooperative, executor-integrated synchronization** essential for embedded async programming. They enable safe shared-state access while maintaining the benefits of async/await - responsive multitasking without blocking, efficient resource usage, and clean async/await syntax for complex concurrent embedded applications.