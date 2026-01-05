# Embassy Signals: Lightweight Async Task Notifications

## Overview

**Embassy Signals** (`embassy_sync::signal::Signal`) are a lightweight, efficient synchronization primitive designed for waking and notifying async tasks in embedded systems. They provide a simple way for one task to signal another that an event has occurred or data is available, without the overhead of channels or more complex synchronization mechanisms.

## Core Characteristics

### What Makes Signals Special

1. **Single-slot storage**: Signals hold exactly one value at a time
2. **Overwriting behavior**: New signals overwrite old ones if not yet consumed
3. **Zero-cost when idle**: Minimal memory footprint (typically just a few bytes)
4. **Wait/notify pattern**: Tasks can wait for signals and be automatically woken when they arrive
5. **Type-safe**: Generic over the signal data type

### Key Differences from Channels

- **Channels**: Queue multiple messages, preserve all values
- **Signals**: Keep only the latest value, designed for notifications rather than data streaming

## Basic Usage Pattern

```rust
use embassy_sync::signal::Signal;
use embassy_sync::blocking_mutex::raw::CriticalSectionRawMutex;

// Define a signal with a specific type
static MY_SIGNAL: Signal<CriticalSectionRawMutex, u32> = Signal::new();

// Sender task/interrupt
#[embassy_executor::task]
async fn sender_task() {
    loop {
        Timer::after(Duration::from_secs(1)).await;
        MY_SIGNAL.signal(42); // Send notification
    }
}

// Receiver task
#[embassy_executor::task]
async fn receiver_task() {
    loop {
        let value = MY_SIGNAL.wait().await; // Wait for signal
        info!("Received signal: {}", value);
    }
}
```

## Practical Examples

### Example 1: Interrupt to Task Communication

One of the most common use cases - an interrupt handler notifying a task:

```rust
use embassy_sync::signal::Signal;
use embassy_sync::blocking_mutex::raw::CriticalSectionRawMutex;

// Signal to notify button press
static BUTTON_PRESSED: Signal<CriticalSectionRawMutex, ()> = Signal::new();

// Interrupt handler
#[interrupt]
fn EXTI0() {
    // Signal the task (non-blocking, safe from interrupt context)
    BUTTON_PRESSED.signal(());
    // Clear interrupt flag
}

// Task that handles button presses
#[embassy_executor::task]
async fn button_handler() {
    loop {
        BUTTON_PRESSED.wait().await;
        info!("Button was pressed!");
        
        // Debounce or handle the press
        Timer::after(Duration::from_millis(200)).await;
    }
}
```

### Example 2: Sensor Data Ready Notification

```rust
use embassy_sync::signal::Signal;
use embassy_sync::blocking_mutex::raw::NoopRawMutex;

#[derive(Clone, Copy)]
struct SensorReading {
    temperature: f32,
    humidity: f32,
}

static SENSOR_DATA: Signal<NoopRawMutex, SensorReading> = Signal::new();

// Sensor polling task
#[embassy_executor::task]
async fn sensor_task(mut sensor: Sensor) {
    loop {
        let reading = sensor.read().await;
        
        // Signal new data is available
        SENSOR_DATA.signal(reading);
        
        Timer::after(Duration::from_secs(5)).await;
    }
}

// Data processing task
#[embassy_executor::task]
async fn processor_task() {
    loop {
        let reading = SENSOR_DATA.wait().await;
        
        if reading.temperature > 30.0 {
            warn!("High temperature: {}°C", reading.temperature);
        }
        
        // Process the data...
    }
}
```

### Example 3: State Machine Event Notification

```rust
use embassy_sync::signal::Signal;
use embassy_sync::blocking_mutex::raw::CriticalSectionRawMutex;

#[derive(Clone, Copy, PartialEq)]
enum SystemEvent {
    PowerOn,
    PowerOff,
    Error(u8),
    DataReady,
}

static SYSTEM_EVENTS: Signal<CriticalSectionRawMutex, SystemEvent> = Signal::new();

// Event dispatcher
#[embassy_executor::task]
async fn event_dispatcher() {
    loop {
        let event = SYSTEM_EVENTS.wait().await;
        
        match event {
            SystemEvent::PowerOn => {
                info!("System powering on...");
                // Initialize systems
            }
            SystemEvent::PowerOff => {
                info!("System shutting down...");
                // Cleanup
            }
            SystemEvent::Error(code) => {
                error!("System error: {}", code);
                // Handle error
            }
            SystemEvent::DataReady => {
                // Process data
            }
        }
    }
}

// Somewhere else in code
fn trigger_shutdown() {
    SYSTEM_EVENTS.signal(SystemEvent::PowerOff);
}
```

### Example 4: Multi-Task Coordination

```rust
use embassy_sync::signal::Signal;
use embassy_sync::blocking_mutex::raw::ThreadModeRawMutex;

static TASK_A_DONE: Signal<ThreadModeRawMutex, ()> = Signal::new();
static TASK_B_DONE: Signal<ThreadModeRawMutex, ()> = Signal::new();

#[embassy_executor::task]
async fn task_a() {
    loop {
        // Do work
        heavy_computation().await;
        
        // Signal completion
        TASK_A_DONE.signal(());
        
        // Wait for task B to complete
        TASK_B_DONE.wait().await;
    }
}

#[embassy_executor::task]
async fn task_b() {
    loop {
        // Wait for task A
        TASK_A_DONE.wait().await;
        
        // Do work
        process_results().await;
        
        // Signal completion
        TASK_B_DONE.signal(());
    }
}
```

## Advanced Patterns

### Try Wait (Non-blocking Check)

```rust
// Check if a signal is available without waiting
match MY_SIGNAL.try_take() {
    Some(value) => {
        info!("Got value: {}", value);
    }
    None => {
        info!("No signal available");
    }
}
```

### Signaling with Unit Type for Pure Notifications

```rust
// When you just need to wake a task without data
static WAKE_UP: Signal<CriticalSectionRawMutex, ()> = Signal::new();

// Send notification
WAKE_UP.signal(());

// Wait for notification
WAKE_UP.wait().await;
```

### Reset Pattern

```rust
// Clear any pending signal
MY_SIGNAL.reset();

// Useful for initialization or cleanup
#[embassy_executor::task]
async fn init_task() {
    // Clear any stale signals from before reset
    ERROR_SIGNAL.reset();
    
    // Now start waiting for fresh signals
    loop {
        let error = ERROR_SIGNAL.wait().await;
        handle_error(error);
    }
}
```

## Choosing the Right Mutex Type

Embassy Signals require a mutex type parameter for thread-safety:

```rust
use embassy_sync::blocking_mutex::raw::*;

// CriticalSectionRawMutex: Most common, works everywhere including interrupts
Signal<CriticalSectionRawMutex, T>

// NoopRawMutex: Single-core, no threading (fastest)
Signal<NoopRawMutex, T>

// ThreadModeRawMutex: Cortex-M thread mode (not interrupt-safe)
Signal<ThreadModeRawMutex, T>
```

## Common Pitfalls and Best Practices

### ✅ Best Practices

1. **Use Unit Type for Simple Notifications**: `Signal<_, ()>` when no data needed
2. **Keep Signal Data Small**: Copy types work best (implement `Clone + Copy`)
3. **Use Descriptive Names**: `BUTTON_PRESSED`, `DATA_READY` not just `SIGNAL1`
4. **Reset When Needed**: Clear stale signals during initialization

### ❌ Common Mistakes

```rust
// ❌ Don't use for streaming data (use Channel instead)
loop {
    SIGNAL.signal(sensor.read()); // Values can be lost!
}

// ✅ Use for latest-value-only notifications
TEMPERATURE_UPDATED.signal(latest_reading);

// ❌ Don't forget the mutex type matters
static SIG: Signal<_, u32> = Signal::new(); // Won't compile!

// ✅ Always specify the mutex
static SIG: Signal<CriticalSectionRawMutex, u32> = Signal::new();
```

## Performance Characteristics

- **Memory**: ~4-12 bytes depending on platform and data type
- **Signal time**: Typically <1μs (just sets a flag and wakes task)
- **Wait overhead**: Minimal when signal already available
- **Interrupt-safe**: Yes (with appropriate mutex type)

## When to Use Signals vs Alternatives

| Use Case | Use Signal | Use Channel | Use PubSub |
|----------|------------|-------------|------------|
| Latest value only | ✅ | ❌ | ❌ |
| All messages matter | ❌ | ✅ | ✅ |
| Single receiver | ✅ | ✅ | ❌ |
| Multiple receivers | ❌ | ❌ | ✅ |
| From interrupts | ✅ | ✅ | ✅ |
| Minimum overhead | ✅ | ❌ | ❌ |

## Summary

Embassy Signals are the go-to primitive for lightweight async task notifications in embedded systems. They excel at:

- **Interrupt-to-task communication**
- **Event notifications where only the latest matters**
- **Simple producer-consumer patterns**
- **Wake-up mechanisms**

Their simplicity, efficiency, and safety make them an essential tool in the Embassy async ecosystem for embedded Rust development.