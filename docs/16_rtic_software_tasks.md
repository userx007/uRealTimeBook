# RTIC Software Tasks: Message-Passing with Compile-Time Guarantees

## Overview

**Software Tasks** in RTIC (Real-Time Interrupt-driven Concurrency) are a powerful concurrency primitive that enables asynchronous, message-passing communication between different parts of your embedded application. Unlike hardware tasks (which are triggered by interrupts), software tasks are spawned programmatically and communicate through type-safe message queues with **compile-time capacity verification**.

## Core Concepts

### 1. **Message-Passing Architecture**

Software tasks implement an actor-like model where:
- Each task has its own message queue
- Tasks are spawned with typed messages (arguments)
- Communication is asynchronous and lock-free
- Queue capacity is defined at compile time and verified by the type system

### 2. **Compile-Time Capacity Checking**

RTIC's type system ensures that:
- Queue overflow is impossible (exceeding capacity is a compile-time error)
- Memory requirements are known statically
- No dynamic allocation is needed
- Resource usage is predictable

## Key Features

### **Priority-Based Scheduling**
- Each software task has a priority level (0-255)
- Higher priority tasks preempt lower priority ones
- The scheduler ensures deadlock-free execution

### **Zero-Cost Abstractions**
- No runtime overhead compared to hand-written interrupt handlers
- Message queues are implemented as lock-free ring buffers
- Static dispatch eliminates virtual function call overhead

## Practical Examples

### Example 1: Basic Software Task with Message Passing

```rust
#[rtic::app(device = stm32f4::stm32f401, dispatchers = [EXTI0, EXTI1])]
mod app {
    use rtic::Monotonic;
    
    #[shared]
    struct Shared {}
    
    #[local]
    struct Local {}
    
    #[init]
    fn init(ctx: init::Context) -> (Shared, Local) {
        // Spawn a software task with a message
        process_data::spawn(42, "Hello").unwrap();
        
        (Shared {}, Local {})
    }
    
    // Software task with capacity of 4 messages
    #[task(capacity = 4)]
    async fn process_data(ctx: process_data::Context, value: u32, message: &'static str) {
        // Process the received message
        defmt::info!("Received: {} = {}", message, value);
        
        // Can spawn other tasks
        display_result::spawn(value * 2).ok();
    }
    
    #[task(capacity = 8, priority = 2)]
    async fn display_result(ctx: display_result::Context, result: u32) {
        defmt::info!("Result: {}", result);
    }
}
```

**Key Points:**
- `capacity = 4` means up to 4 messages can be queued
- Attempting to spawn more will result in a compile-time checked error
- Messages are passed by value (value) or reference (message)

### Example 2: Producer-Consumer Pattern

```rust
#[rtic::app(device = nrf52840_hal::pac, dispatchers = [TIMER0, TIMER1])]
mod app {
    #[shared]
    struct Shared {}
    
    #[local]
    struct Local {
        counter: u32,
    }
    
    #[init]
    fn init(ctx: init::Context) -> (Shared, Local) {
        // Start the producer
        producer::spawn().unwrap();
        
        (Shared {}, Local { counter: 0 })
    }
    
    // Producer task - generates data periodically
    #[task(capacity = 1)]
    async fn producer(ctx: producer::Context) {
        for i in 0..100 {
            // Send data to consumer
            match consumer::spawn(i, i * i) {
                Ok(_) => defmt::info!("Sent: {}", i),
                Err(_) => defmt::warn!("Consumer queue full!"),
            }
            
            // Simulate periodic production
            cortex_m::asm::delay(1_000_000);
        }
    }
    
    // Consumer task - processes incoming data
    #[task(capacity = 16, priority = 2)]
    async fn consumer(ctx: consumer::Context, id: u32, value: u32) {
        // Process the data
        defmt::info!("Processing id={}, value={}", id, value);
        
        // Heavy computation simulation
        let result = value.wrapping_mul(2);
        
        // Forward to next stage
        logger::spawn(id, result).ok();
    }
    
    #[task(capacity = 32, priority = 1)]
    async fn logger(ctx: logger::Context, id: u32, data: u32) {
        defmt::info!("Log: id={} -> {}", id, data);
    }
}
```

**Demonstrates:**
- Multi-stage pipeline with different queue capacities
- Priority levels (consumer has higher priority than logger)
- Error handling when queues are full
- Compile-time verification of queue depths

### Example 3: Sensor Data Processing Pipeline

```rust
#[rtic::app(device = esp32::Peripherals, dispatchers = [GPIO, UART])]
mod app {
    use heapless::Vec;
    
    #[derive(Debug, Clone)]
    struct SensorReading {
        sensor_id: u8,
        temperature: f32,
        timestamp: u64,
    }
    
    #[shared]
    struct Shared {}
    
    #[local]
    struct Local {}
    
    #[init]
    fn init(ctx: init::Context) -> (Shared, Local) {
        (Shared {}, Local {})
    }
    
    // Hardware interrupt spawns software task
    #[task(binds = GPIO, priority = 3)]
    fn sensor_interrupt(ctx: sensor_interrupt::Context) {
        // Spawn with sensor data
        let reading = SensorReading {
            sensor_id: 1,
            temperature: 23.5,
            timestamp: 1000,
        };
        
        filter_data::spawn(reading).unwrap();
    }
    
    // First stage: filter noise
    #[task(capacity = 8, priority = 2)]
    async fn filter_data(ctx: filter_data::Context, mut reading: SensorReading) {
        // Apply filtering
        if reading.temperature > 100.0 || reading.temperature < -40.0 {
            defmt::warn!("Out of range reading discarded");
            return;
        }
        
        // Forward to calibration
        calibrate_data::spawn(reading).ok();
    }
    
    // Second stage: calibrate
    #[task(capacity = 8, priority = 2)]
    async fn calibrate_data(ctx: calibrate_data::Context, mut reading: SensorReading) {
        // Apply calibration curve
        reading.temperature = reading.temperature * 1.05 - 0.3;
        
        // Forward to storage
        store_data::spawn(reading).ok();
    }
    
    // Final stage: store
    #[task(capacity = 16, priority = 1)]
    async fn store_data(ctx: store_data::Context, reading: SensorReading) {
        defmt::info!("Storing: {:?}", reading);
        // Write to flash, send over network, etc.
    }
}
```

**Illustrates:**
- Hardware interrupt triggering software task chain
- Data transformation pipeline
- Different priority levels for different stages
- Structured data passing between tasks

## Compile-Time Capacity Checking in Action

### What Gets Checked

```rust
#[task(capacity = 2)]
async fn my_task(ctx: my_task::Context, data: u32) {
    // Task implementation
}

// In another task:
my_task::spawn(1)?; // OK - 1 message in queue
my_task::spawn(2)?; // OK - 2 messages in queue
my_task::spawn(3)?; // Runtime error if queue full
                    // But capacity is known at compile time!
```

The RTIC compiler:
- **Allocates exactly 2 slots** in the task's message queue
- **Generates spawn() method** that returns `Result<(), SpawnError>`
- **Statically computes memory layout** for zero runtime allocation

### Memory Layout Example

```
For: #[task(capacity = 4)]
     async fn process(data: u32, flag: bool) { ... }

Generated Queue Structure:
┌─────────────────────────────┐
│ Message 1: (u32, bool)      │
│ Message 2: (u32, bool)      │
│ Message 3: (u32, bool)      │
│ Message 4: (u32, bool)      │
└─────────────────────────────┘
Total: 4 × (4 bytes + 1 byte) = 20 bytes + metadata
```

## Benefits

1. **Predictable Timing**: Queue depths known at compile time enable WCET analysis
2. **Memory Safety**: No buffer overflows, no dynamic allocation
3. **Type Safety**: Messages are strongly typed
4. **Deadlock Freedom**: Priority-based scheduling with resource ceiling protocol
5. **Zero Overhead**: Compiles to efficient machine code
6. **Composability**: Tasks can be combined into complex workflows

## Common Patterns

### Event Broadcasting
```rust
#[task(capacity = 1)]
async fn event_source(ctx: event_source::Context, event: Event) {
    // Broadcast to multiple consumers
    handler1::spawn(event.clone()).ok();
    handler2::spawn(event.clone()).ok();
    handler3::spawn(event).ok();
}
```

### Rate Limiting
```rust
#[task(capacity = 1)] // Small capacity = natural rate limiting
async fn rate_limited_task(ctx: rate_limited_task::Context) {
    // Only processes one message at a time
    // Additional spawns fail if capacity exceeded
}
```

### Priority Inversion Prevention
```rust
#[task(capacity = 4, priority = 3)] // High priority
async fn critical_task(ctx: critical_task::Context) { }

#[task(capacity = 16, priority = 1)] // Low priority, larger queue
async fn background_task(ctx: background_task::Context) { }
```

## Limitations and Considerations

- **Static Capacity**: Queue size cannot change at runtime
- **Spawning Overhead**: Failed spawns must be handled explicitly
- **Memory Usage**: Large capacities or large message types consume RAM
- **Priority Design**: Requires careful analysis of task priorities

Software tasks in RTIC provide a robust, efficient foundation for building complex embedded applications with strong compile-time guarantees about resource usage and timing behavior.