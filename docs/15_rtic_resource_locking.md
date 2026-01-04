# RTIC-Specific Mechanisms: Resource Locking via RTFM Protocol

## Overview

**RTIC (Real-Time Interrupt-driven Concurrency)** is a Rust framework for building concurrent embedded systems that leverages the **Stack Resource Policy (SRP)** for resource management. Unlike traditional mutex-based locking, RTIC uses **priority ceiling protocols** to provide compile-time guarantees of deadlock freedom and bounded priority inversion.

## Stack Resource Policy (SRP) Fundamentals

### Core Concept

Instead of using locks that block tasks, SRP works by temporarily **raising the execution priority** to prevent preemption by tasks that might access the same resource. This approach:

- **Eliminates blocking**: No task ever waits for a lock
- **Prevents deadlocks**: Impossible by design
- **Bounds priority inversion**: Maximum inversion is one priority level
- **Provides compile-time verification**: The Rust compiler checks resource access patterns

### Priority Ceiling

Each shared resource has a **ceiling priority** equal to the highest priority of any task that accesses it. When a task accesses the resource, the system raises the current priority to this ceiling, preventing preemption by any task that might also need that resource.

## How It Works

### Traditional Mutex Approach (What RTIC Avoids)

```rust
// Traditional approach - what RTIC does NOT do
static SHARED_DATA: Mutex<u32> = Mutex::new(0);

fn low_priority_task() {
    let mut data = SHARED_DATA.lock(); // Blocks if locked
    *data += 1;
    // Task holds lock - can be preempted
}

fn high_priority_task() {
    let mut data = SHARED_DATA.lock(); // May wait indefinitely
    *data += 2;
}
```

**Problems:**
- Potential deadlocks with multiple locks
- Unbounded priority inversion
- Runtime overhead of lock management

### RTIC's SRP Approach

```rust
#[rtic::app(device = stm32f4)]
mod app {
    use rtic::Mutex;

    #[shared]
    struct Shared {
        counter: u32,  // Ceiling priority = max(task1, task2) = 2
    }

    #[local]
    struct Local {}

    #[init]
    fn init(cx: init::Context) -> (Shared, Local) {
        (Shared { counter: 0 }, Local {})
    }

    // Priority 1 task
    #[task(shared = [counter], priority = 1)]
    fn task1(mut cx: task1::Context) {
        cx.shared.counter.lock(|counter| {
            // Priority raised to 2 (ceiling)
            // task2 cannot preempt here
            *counter += 1;
            // Priority restored to 1 when lock drops
        });
    }

    // Priority 2 task
    #[task(shared = [counter], priority = 2)]
    fn task2(mut cx: task2::Context) {
        cx.shared.counter.lock(|counter| {
            // Already at priority 2
            // No need to raise priority
            *counter += 10;
        });
    }
}
```

## Detailed Examples

### Example 1: Multiple Resources with Different Ceilings

```rust
#[rtic::app(device = stm32f4)]
mod app {
    #[shared]
    struct Shared {
        sensor_data: i32,    // Ceiling = 3 (used by task1, task3)
        display_buffer: [u8; 64], // Ceiling = 2 (used by task1, task2)
    }

    // Priority 1: Low priority periodic task
    #[task(shared = [sensor_data, display_buffer], priority = 1)]
    fn update_display(mut cx: update_display::Context) {
        // Access display_buffer (ceiling 2)
        cx.shared.display_buffer.lock(|buffer| {
            // Priority raised to 2
            // task2 cannot interrupt, but task3 can
            buffer[0] = 0xFF;
        });

        // Access sensor_data (ceiling 3)
        cx.shared.sensor_data.lock(|data| {
            // Priority raised to 3
            // Neither task2 nor task3 can interrupt
            *data = read_sensor();
        });
    }

    // Priority 2: Medium priority
    #[task(shared = [display_buffer], priority = 2)]
    fn refresh_screen(mut cx: refresh_screen::Context) {
        cx.shared.display_buffer.lock(|buffer| {
            // Already at priority 2
            send_to_display(buffer);
        });
    }

    // Priority 3: High priority interrupt
    #[task(shared = [sensor_data], priority = 3)]
    fn sensor_interrupt(mut cx: sensor_interrupt::Context) {
        cx.shared.sensor_data.lock(|data| {
            // Already at priority 3
            *data = read_adc();
        });
    }
}
```

### Example 2: Compile-Time Deadlock Prevention

```rust
// This code would COMPILE and be SAFE in RTIC
#[task(shared = [resource_a, resource_b], priority = 1)]
fn task_x(mut cx: task_x::Context) {
    cx.shared.resource_a.lock(|a| {
        cx.shared.resource_b.lock(|b| {
            // Nested access is safe - priority raised to max ceiling
            *a += *b;
        });
    });
}

#[task(shared = [resource_b, resource_a], priority = 2)]
fn task_y(mut cx: task_y::Context) {
    // Different lock order - but still safe!
    cx.shared.resource_b.lock(|b| {
        cx.shared.resource_a.lock(|a| {
            *b += *a;
        });
    });
}
```

**Why this is safe:** When `task_x` enters the first lock, its priority is raised to the maximum ceiling of all resources it might access. This prevents `task_y` from preempting it, eliminating the circular wait condition.

### Example 3: Zero-Cost Abstraction

```rust
#[task(shared = [counter], priority = 3)]
fn high_priority_exclusive(mut cx: high_priority_exclusive::Context) {
    // If this is the highest priority task accessing counter,
    // RTIC optimizes away the lock entirely at compile time!
    cx.shared.counter.lock(|counter| {
        *counter += 1;  // Direct access, no runtime cost
    });
}

#[task(shared = [counter], priority = 1)]
fn low_priority_task(mut cx: low_priority_task::Context) {
    // This task needs actual priority manipulation
    cx.shared.counter.lock(|counter| {
        *counter += 1;  // Runtime: disable interrupts up to priority 3
    });
}
```

## Key Advantages Over Traditional Locking

### 1. **Deadlock Freedom**
```rust
// Impossible to create deadlock - compiler enforces SRP
// All resource orderings are safe
task_a: lock(X) → lock(Y)
task_b: lock(Y) → lock(X)  // Still safe!
```

### 2. **Bounded Priority Inversion**
```rust
// Maximum inversion = one priority level
Low priority (1):  [====lock(R)====]
                        ↑ priority raised to 3
High priority (3):     [...waits max one critical section...]
```

### 3. **Compile-Time Verification**
```rust
// Compiler error: task doesn't declare shared resource access
#[task(priority = 1)]  // Missing: shared = [counter]
fn buggy_task(cx: buggy_task::Context) {
    cx.shared.counter.lock(|c| *c += 1);  // ❌ Compile error
}
```

### 4. **Zero-Cost for Exclusive Access**
```rust
// If only highest-priority task accesses resource,
// lock() compiles to nothing - direct access
#[task(shared = [exclusive_data], priority = 255)]
fn exclusive_task(mut cx: exclusive_task::Context) {
    cx.shared.exclusive_data.lock(|data| {
        // Compiles to: *data = value;  (no interrupt manipulation)
        *data = 42;
    });
}
```

## Implementation Under the Hood

The RTIC framework generates code that manipulates the **NVIC (Nested Vectored Interrupt Controller)** priority levels:

```rust
// Simplified pseudo-code of what RTIC generates
impl Mutex for SharedResource {
    fn lock<R, F>(&mut self, f: F) -> R 
    where F: FnOnce(&mut T) -> R 
    {
        // Save current priority mask
        let old_mask = read_priority_mask();
        
        // Raise to ceiling priority
        write_priority_mask(CEILING_PRIORITY);
        
        // Execute critical section
        let result = f(&mut self.data);
        
        // Restore original priority
        write_priority_mask(old_mask);
        
        result
    }
}
```

## Comparison Summary

| Aspect | Traditional Mutex | RTIC SRP |
|--------|------------------|----------|
| **Deadlock** | Possible | Impossible |
| **Priority Inversion** | Unbounded | Bounded (one level) |
| **Verification** | Runtime | Compile-time |
| **Blocking** | Tasks wait | No blocking |
| **Overhead** | Always present | Often zero-cost |
| **Code Size** | Larger | Smaller |

## Real-World Example: Motor Control System

```rust
#[rtic::app(device = stm32f4)]
mod motor_controller {
    #[shared]
    struct Shared {
        motor_speed: f32,      // Ceiling = 3 (control loop + safety)
        position: i32,         // Ceiling = 2 (control loop + display)
        error_flags: u32,      // Ceiling = 3 (safety has highest priority)
    }

    // Safety monitor - highest priority
    #[task(shared = [motor_speed, error_flags], priority = 3)]
    fn safety_check(mut cx: safety_check::Context) {
        (cx.shared.motor_speed, cx.shared.error_flags)
            .lock(|speed, flags| {
                if *speed > MAX_SPEED {
                    *flags |= OVERSPEED_FLAG;
                    *speed = 0.0;  // Emergency stop
                }
            });
    }

    // Control loop - medium priority
    #[task(shared = [motor_speed, position], priority = 2)]
    fn control_loop(mut cx: control_loop::Context) {
        let new_speed = (cx.shared.motor_speed, cx.shared.position)
            .lock(|speed, pos| {
                let error = TARGET_POS - *pos;
                *speed = pid_control(error);
                *speed
            });
        
        set_pwm(new_speed);
    }

    // Display update - low priority
    #[task(shared = [position], priority = 1)]
    fn update_display(mut cx: update_display::Context) {
        let pos = cx.shared.position.lock(|p| *p);
        display.print(pos);
    }
}
```

This example demonstrates how RTIC's SRP ensures that the safety check can always preempt other tasks, while the control loop has guaranteed access to shared state without risk of deadlock or unbounded delays.