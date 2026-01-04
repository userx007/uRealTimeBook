# Embassy-Specific Mechanisms: Async Channels

## Overview

Embassy's async channels (`embassy_sync::channel`) are message-passing primitives specifically designed for embedded systems that integrate seamlessly with Rust's async/await syntax. They provide a safe, efficient way for tasks to communicate without shared mutable state, making them ideal for concurrent embedded applications.

## Core Concepts

### What Makes Embassy Channels Special

Unlike standard library channels, Embassy channels are:
- **No-std compatible** - Work without heap allocation or operating system
- **Interrupt-safe** - Can be used from interrupt handlers
- **Zero-cost abstractions** - Minimal overhead for embedded systems
- **Static allocation** - Memory is allocated at compile time
- **Async-native** - Built from the ground up for async/await

## Channel Types

### 1. **Bounded Channels** (`Channel`)

Bounded channels have a fixed capacity set at compile time. When full, senders will wait asynchronously until space becomes available.

**Key characteristics:**
- Fixed buffer size known at compile time
- Senders block when buffer is full
- Receivers block when buffer is empty
- Zero dynamic allocation

**Example: Sensor Data Pipeline**

```rust
use embassy_sync::channel::Channel;
use embassy_sync::blocking_mutex::raw::ThreadModeRawMutex;

// Create a channel that can hold up to 10 sensor readings
static SENSOR_CHANNEL: Channel<ThreadModeRawMutex, SensorReading, 10> = 
    Channel::new();

#[derive(Clone, Copy)]
struct SensorReading {
    temperature: f32,
    timestamp: u64,
}

// Producer task: reads sensor and sends data
#[embassy_executor::task]
async fn sensor_reader_task() {
    loop {
        // Read sensor (simulated)
        let reading = SensorReading {
            temperature: read_temperature_sensor().await,
            timestamp: get_timestamp(),
        };
        
        // Send to channel - will wait if buffer is full
        SENSOR_CHANNEL.send(reading).await;
        
        Timer::after(Duration::from_millis(100)).await;
    }
}

// Consumer task: processes sensor data
#[embassy_executor::task]
async fn data_processor_task() {
    loop {
        // Receive from channel - will wait if buffer is empty
        let reading = SENSOR_CHANNEL.receive().await;
        
        // Process the data
        if reading.temperature > 30.0 {
            trigger_cooling_system().await;
        }
        
        log_data(reading).await;
    }
}
```

### 2. **Unbounded Channels** (`PubSubChannel`)

Embassy's publish-subscribe channels allow multiple subscribers to receive the same messages. Each subscriber has its own queue.

**Example: Event Broadcasting**

```rust
use embassy_sync::pubsub::{PubSubChannel, Publisher, Subscriber};
use embassy_sync::blocking_mutex::raw::ThreadModeRawMutex;

#[derive(Clone, Copy)]
enum SystemEvent {
    ButtonPressed,
    LowBattery,
    NetworkConnected,
}

// PubSub channel with capacity for 4 messages per subscriber, max 3 subscribers
static EVENT_BUS: PubSubChannel<ThreadModeRawMutex, SystemEvent, 4, 3, 1> = 
    PubSubChannel::new();

// Publisher task
#[embassy_executor::task]
async fn event_publisher_task() {
    let publisher = EVENT_BUS.publisher().unwrap();
    
    loop {
        if button_is_pressed() {
            publisher.publish(SystemEvent::ButtonPressed).await;
        }
        Timer::after(Duration::from_millis(50)).await;
    }
}

// Subscriber task 1: UI updates
#[embassy_executor::task]
async fn ui_task() {
    let mut subscriber = EVENT_BUS.subscriber().unwrap();
    
    loop {
        match subscriber.next_message().await {
            SystemEvent::ButtonPressed => update_ui_button_state(),
            SystemEvent::LowBattery => show_battery_warning(),
            SystemEvent::NetworkConnected => show_network_icon(),
        }
    }
}

// Subscriber task 2: Logging
#[embassy_executor::task]
async fn logging_task() {
    let mut subscriber = EVENT_BUS.subscriber().unwrap();
    
    loop {
        let event = subscriber.next_message().await;
        log_event_to_flash(event).await;
    }
}
```

## Advanced Patterns

### 3. **Multiple Producers, Single Consumer (MPSC)**

```rust
use embassy_sync::channel::Channel;
use embassy_sync::blocking_mutex::raw::CriticalSectionRawMutex;

static COMMAND_CHANNEL: Channel<CriticalSectionRawMutex, Command, 16> = 
    Channel::new();

#[derive(Clone, Copy)]
enum Command {
    Start,
    Stop,
    Configure { speed: u32 },
}

// Multiple producer tasks can send to the same channel
#[embassy_executor::task]
async fn uart_command_parser() {
    loop {
        let cmd = parse_uart_command().await;
        COMMAND_CHANNEL.send(cmd).await;
    }
}

#[embassy_executor::task]
async fn button_handler() {
    loop {
        wait_for_button_press().await;
        COMMAND_CHANNEL.send(Command::Start).await;
    }
}

// Single consumer processes all commands
#[embassy_executor::task]
async fn motor_controller() {
    loop {
        match COMMAND_CHANNEL.receive().await {
            Command::Start => start_motor(),
            Command::Stop => stop_motor(),
            Command::Configure { speed } => set_motor_speed(speed),
        }
    }
}
```

### 4. **Try Operations for Non-Blocking Access**

```rust
use embassy_sync::channel::Channel;
use embassy_sync::blocking_mutex::raw::ThreadModeRawMutex;

static LOG_CHANNEL: Channel<ThreadModeRawMutex, LogEntry, 32> = Channel::new();

// Non-blocking send from interrupt context
fn interrupt_handler() {
    let entry = LogEntry::new("Interrupt triggered");
    
    // Try to send without blocking - drops message if channel is full
    if LOG_CHANNEL.try_send(entry).is_err() {
        // Channel full, message dropped (acceptable for logging)
    }
}

// Logger task processes when available
#[embassy_executor::task]
async fn logger_task() {
    loop {
        let entry = LOG_CHANNEL.receive().await;
        write_to_flash(entry).await;
    }
}
```

## Mutex Types

Embassy channels require a mutex type parameter that determines the synchronization primitive:

- **`ThreadModeRawMutex`** - For single-core, thread-mode only access
- **`CriticalSectionRawMutex`** - For interrupt-safe access (disables interrupts)
- **`NoopRawMutex`** - For single-threaded contexts (no locking overhead)

```rust
// Interrupt-safe channel
static INTERRUPT_CHANNEL: Channel<CriticalSectionRawMutex, u32, 8> = 
    Channel::new();

// Thread-mode only (more efficient)
static THREAD_CHANNEL: Channel<ThreadModeRawMutex, u32, 8> = 
    Channel::new();
```

## Best Practices

1. **Choose appropriate buffer sizes** - Balance memory usage with throughput needs
2. **Use static channels** - Avoid dynamic allocation in embedded contexts
3. **Select correct mutex type** - Match synchronization needs to prevent deadlocks
4. **Handle backpressure** - Design systems to handle full channels gracefully
5. **Prefer bounded channels** - Provide deterministic memory usage
6. **Use try_send/try_receive** - For time-critical or interrupt contexts

## Real-World Use Case: IoT Device

```rust
use embassy_sync::channel::Channel;
use embassy_sync::blocking_mutex::raw::CriticalSectionRawMutex;

// Sensor data pipeline
static SENSOR_DATA: Channel<CriticalSectionRawMutex, SensorPacket, 10> = 
    Channel::new();

// Network transmission queue  
static TX_QUEUE: Channel<CriticalSectionRawMutex, NetworkPacket, 20> = 
    Channel::new();

#[embassy_executor::task]
async fn sensor_sampling() {
    loop {
        let data = read_all_sensors().await;
        SENSOR_DATA.send(data).await;
        Timer::after(Duration::from_secs(1)).await;
    }
}

#[embassy_executor::task]
async fn data_aggregator() {
    let mut buffer = Vec::new();
    
    loop {
        let packet = SENSOR_DATA.receive().await;
        buffer.push(packet);
        
        if buffer.len() >= 10 {
            let aggregated = compress_and_aggregate(&buffer);
            TX_QUEUE.send(aggregated).await;
            buffer.clear();
        }
    }
}

#[embassy_executor::task]
async fn network_transmitter() {
    loop {
        let packet = TX_QUEUE.receive().await;
        send_to_cloud(packet).await;
    }
}
```

Embassy's async channels provide the foundation for building responsive, concurrent embedded applications with minimal overhead and maximum safety—perfect for resource-constrained environments where every byte and cycle counts.