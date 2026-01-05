# Embassy Watch/WatchBehavior: Broadcasting State Changes

## Overview

**Watch** and **WatchBehavior** are Embassy synchronization primitives designed for **one-to-many communication** where a single sender broadcasts state changes to multiple subscribers. They're particularly useful in embedded systems where multiple tasks need to react to shared state updates.

## Key Characteristics

### Watch
- **Single producer, multiple consumers** (SPMC) pattern
- Subscribers only see the **latest value** (not a queue)
- **Lossy**: Intermediate updates may be skipped if not consumed in time
- **Efficient**: No memory overhead for missed updates
- Uses `Sender` and `Receiver` model

### WatchBehavior
- Similar to `Watch` but with **behavioral semantics**
- Guarantees that all receivers see **at least one value** when they first subscribe
- Better suited for representing "current state" that should always be available
- Uses `Sender` and `Receiver` model

## When to Use

**Use Watch/WatchBehavior when:**
- Multiple tasks need to monitor the same state
- Only the latest value matters (not every intermediate change)
- You want memory-efficient broadcasting
- Examples: sensor readings, system status, configuration changes

**Don't use when:**
- You need a queue (every message must be processed) → Use `Channel` instead
- You need bidirectional communication → Use `Channel` or other primitives
- You have multiple producers → Use `PubSubChannel` instead

## Core Concepts

### 1. **Latest-Value Semantics**
```
Sender updates: A → B → C → D
Slow receiver reads: A ... D
(B and C were skipped because only latest matters)
```

### 2. **Automatic Wake-up**
When the sender updates the value, all waiting receivers are automatically woken up.

### 3. **Memory Efficiency**
Only stores the current value, not a history of changes.

## Practical Examples

### Example 1: Temperature Monitoring System

```rust
use embassy_sync::watch::Watch;
use embassy_sync::blocking_mutex::raw::CriticalSectionRawMutex;

// Shared temperature value
static TEMPERATURE: Watch<CriticalSectionRawMutex, i32, 4> = Watch::new();

#[embassy_executor::task]
async fn temperature_sensor() {
    let sender = TEMPERATURE.sender();
    let mut temp = 20;
    
    loop {
        // Simulate reading from sensor
        temp = read_sensor().await;
        
        // Broadcast to all subscribers
        sender.send(temp);
        
        Timer::after_millis(1000).await;
    }
}

#[embassy_executor::task]
async fn display_task() {
    let mut receiver = TEMPERATURE.receiver().unwrap();
    
    loop {
        // Wait for temperature updates
        let temp = receiver.changed().await;
        update_display(temp);
    }
}

#[embassy_executor::task]
async fn alarm_task() {
    let mut receiver = TEMPERATURE.receiver().unwrap();
    
    loop {
        let temp = receiver.changed().await;
        
        if temp > 80 {
            trigger_alarm();
        }
    }
}

#[embassy_executor::task]
async fn logging_task() {
    let mut receiver = TEMPERATURE.receiver().unwrap();
    
    loop {
        let temp = receiver.changed().await;
        log_to_flash(temp).await;
    }
}
```

### Example 2: System State Management with WatchBehavior

```rust
use embassy_sync::watch::WatchBehavior;

#[derive(Clone, Copy, PartialEq)]
enum SystemState {
    Idle,
    Active,
    Sleep,
    Error,
}

static STATE: WatchBehavior<CriticalSectionRawMutex, SystemState, 3> = 
    WatchBehavior::new();

#[embassy_executor::task]
async fn state_manager() {
    let sender = STATE.sender();
    
    // Initialize with Idle state
    sender.send(SystemState::Idle);
    
    loop {
        let new_state = determine_next_state().await;
        sender.send(new_state);
        Timer::after_millis(100).await;
    }
}

#[embassy_executor::task]
async fn led_controller() {
    let mut receiver = STATE.receiver().unwrap();
    
    loop {
        let state = receiver.changed().await;
        
        match state {
            SystemState::Idle => set_led_blue(),
            SystemState::Active => set_led_green(),
            SystemState::Sleep => set_led_off(),
            SystemState::Error => set_led_red(),
        }
    }
}

#[embassy_executor::task]
async fn power_manager() {
    let mut receiver = STATE.receiver().unwrap();
    
    loop {
        let state = receiver.changed().await;
        
        if state == SystemState::Sleep {
            enter_low_power_mode().await;
        } else {
            exit_low_power_mode().await;
        }
    }
}
```

### Example 3: Configuration Broadcasting

```rust
#[derive(Clone, Copy)]
struct Config {
    sample_rate: u32,
    threshold: i32,
    enabled: bool,
}

static CONFIG: Watch<CriticalSectionRawMutex, Config, 5> = Watch::new();

#[embassy_executor::task]
async fn config_update_task() {
    let sender = CONFIG.sender();
    
    // Initial config
    sender.send(Config {
        sample_rate: 100,
        threshold: 50,
        enabled: true,
    });
    
    loop {
        // Wait for config updates from external source
        if let Some(new_config) = check_for_config_update().await {
            sender.send(new_config);
        }
        Timer::after_millis(5000).await;
    }
}

#[embassy_executor::task]
async fn sensor_task() {
    let mut receiver = CONFIG.receiver().unwrap();
    
    loop {
        // Get latest config
        let config = receiver.changed().await;
        
        // Apply new sample rate
        adjust_sampling(config.sample_rate).await;
    }
}

#[embassy_executor::task]
async fn processing_task() {
    let mut receiver = CONFIG.receiver().unwrap();
    
    loop {
        let config = receiver.changed().await;
        
        // Update processing threshold
        set_threshold(config.threshold);
        
        if config.enabled {
            process_data().await;
        }
    }
}
```

## API Reference

### Watch Creation
```rust
// Create a Watch with capacity for N receivers
static MY_WATCH: Watch<RawMutex, ValueType, N> = Watch::new();
```

### Sender Operations
```rust
let sender = MY_WATCH.sender();
sender.send(new_value);  // Broadcast to all receivers
```

### Receiver Operations
```rust
let mut receiver = MY_WATCH.receiver().unwrap();

// Wait for next change (blocking)
let value = receiver.changed().await;

// Get current value without waiting
let current = receiver.get();

// Check if value has changed since last read
if receiver.has_changed() {
    let value = receiver.get();
}
```

## Watch vs WatchBehavior

| Feature | Watch | WatchBehavior |
|---------|-------|---------------|
| Initial value | No guaranteed initial read | Guarantees initial value |
| First read | May block if no update yet | Returns immediately with current state |
| Use case | Event-driven updates | State representation |
| Example | "Temperature changed" | "Current temperature is..." |

## Performance Considerations

1. **No Copying on Send**: Values are stored once and referenced by receivers
2. **Zero Allocation**: Fixed memory usage regardless of update frequency
3. **Fast Wake-up**: Uses efficient wait queue mechanism
4. **Lossy by Design**: Missing intermediate values is intentional and efficient

## Common Patterns

### Pattern 1: Graceful Degradation
```rust
// Display task can skip frames if updates come too fast
loop {
    let value = receiver.changed().await;
    // Only render latest, don't worry about missed frames
    render(value);
}
```

### Pattern 2: State Synchronization
```rust
// Multiple peripherals stay in sync with system state
static MODE: WatchBehavior<_, OperatingMode, 4> = WatchBehavior::new();

// All peripherals automatically get mode changes
```

### Pattern 3: Throttled Updates
```rust
loop {
    // Even if sensor updates at 1kHz,
    // UI only updates when it's ready
    let latest = receiver.changed().await;
    slow_display_update(latest).await;
}
```

## Summary

Watch and WatchBehavior are essential tools in Embassy for efficient state broadcasting in embedded systems. They excel when:
- Multiple tasks need to observe the same state
- Only the latest value matters
- Memory efficiency is important
- You want automatic subscriber wake-up

They're the go-to choice for monitoring sensor values, system states, configuration changes, and any scenario where "latest wins" semantics make sense.