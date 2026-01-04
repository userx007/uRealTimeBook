# Seqlocks: A Deep Dive with Cross-Platform Examples

## Core Concept

**Seqlocks** (sequence locks) are a reader-writer synchronization mechanism optimized for read-heavy workloads where writes are infrequent. Unlike traditional reader-writer locks, seqlocks allow readers to proceed **without blocking**, even when a writer is active. Readers detect concurrent modifications through a sequence counter and simply retry if data was corrupted during reading.

### Key Characteristics:
- **Lock-free reads**: Readers never block writers or other readers
- **Write priority**: Writers get exclusive access when needed
- **Optimistic concurrency**: Readers assume no concurrent writes and verify afterward
- **Retry mechanism**: Readers detect torn reads and retry the operation
- **Best for**: Small data structures that can be read quickly

---

## 1. Linux Kernel Implementation

In the Linux kernel, seqlocks are used extensively for time-keeping, network statistics, and other frequently-read data structures.

### How It Works:

```c
// Linux kernel seqlock structure
typedef struct {
    struct seqcount seqcount;
    spinlock_t lock;
} seqlock_t;

// Reading with seqlock
unsigned int seq;
do {
    seq = read_seqbegin(&my_seqlock);
    
    // Read data (may be torn if writer is active)
    value1 = shared_data.field1;
    value2 = shared_data.field2;
    
} while (read_seqretry(&my_seqlock, seq));
// Loop until we get a consistent read

// Writing with seqlock
write_seqlock(&my_seqlock);
shared_data.field1 = new_value1;
shared_data.field2 = new_value2;
write_sequnlock(&my_seqlock);
```

### Real Linux Kernel Example: Time-Keeping

```c
// Simplified from kernel/time/timekeeping.c
static struct {
    seqcount_t seq;
    u64 xtime_sec;
    u64 xtime_nsec;
    // ... other time fields
} tk_core;

// Reader: Getting current time (called millions of times per second)
ktime_t ktime_get(void)
{
    unsigned int seq;
    ktime_t base;
    u64 nsecs;

    do {
        seq = read_seqcount_begin(&tk_core.seq);
        
        base = tk_core.xtime_sec;
        nsecs = tk_core.xtime_nsec;
        
    } while (read_seqcount_retry(&tk_core.seq, seq));

    return ktime_add_ns(base, nsecs);
}

// Writer: Updating time (happens much less frequently)
void update_wall_time(void)
{
    write_seqcount_begin(&tk_core.seq);
    
    // Update time fields
    tk_core.xtime_sec = new_sec;
    tk_core.xtime_nsec = new_nsec;
    
    write_seqcount_end(&tk_core.seq);
}
```

### The Sequence Counter Logic:

```c
// The sequence number is:
// - EVEN when no write is in progress
// - ODD when a write is in progress

write_seqcount_begin() {
    seq++;  // Make it odd - signals write in progress
    smp_wmb();  // Memory barrier
}

write_seqcount_end() {
    smp_wmb();  // Memory barrier
    seq++;  // Make it even - write complete
}

read_seqcount_begin() {
    seq = READ_ONCE(sequence);
    smp_rmb();  // Memory barrier
    return seq;
}

read_seqcount_retry(seq) {
    smp_rmb();
    return (sequence != seq) || (seq & 1);  // Changed or odd?
}
```

---

## 2. FreeRTOS Implementation

FreeRTOS doesn't have built-in seqlocks, but we can implement the pattern using atomic operations and critical sections.

```c
// FreeRTOS seqlock implementation
typedef struct {
    volatile uint32_t sequence;
    portMUX_TYPE mutex;  // For ESP32, or use taskENTER_CRITICAL
} seqlock_t;

typedef struct {
    int temperature;
    int humidity;
    uint32_t timestamp;
} sensor_data_t;

seqlock_t data_lock = {0};
sensor_data_t sensor_data;

// Reader task (high frequency - called every 10ms)
void vReaderTask(void *pvParameters)
{
    sensor_data_t local_copy;
    uint32_t seq;
    
    while(1) {
        do {
            // Read sequence number (must be even to start)
            do {
                seq = data_lock.sequence;
            } while (seq & 1);  // Wait if odd (write in progress)
            
            portMEMORY_BARRIER();
            
            // Read data (optimistic - might be torn)
            local_copy.temperature = sensor_data.temperature;
            local_copy.humidity = sensor_data.humidity;
            local_copy.timestamp = sensor_data.timestamp;
            
            portMEMORY_BARRIER();
            
            // Check if data changed during read
        } while (data_lock.sequence != seq);
        
        // Use consistent data
        printf("Temp: %d, Humidity: %d\n", 
               local_copy.temperature, local_copy.humidity);
        
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

// Writer task (low frequency - updates every 1 second)
void vWriterTask(void *pvParameters)
{
    while(1) {
        // Read sensor hardware
        int new_temp = read_temperature_sensor();
        int new_humid = read_humidity_sensor();
        
        // Acquire write lock
        taskENTER_CRITICAL(&data_lock.mutex);
        
        // Increment sequence (make it odd)
        data_lock.sequence++;
        portMEMORY_BARRIER();
        
        // Update shared data
        sensor_data.temperature = new_temp;
        sensor_data.humidity = new_humid;
        sensor_data.timestamp = xTaskGetTickCount();
        
        portMEMORY_BARRIER();
        
        // Increment sequence again (make it even)
        data_lock.sequence++;
        
        taskEXIT_CRITICAL(&data_lock.mutex);
        
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
```

### FreeRTOS Use Case: Network Statistics

```c
// Network statistics that many tasks read frequently
typedef struct {
    uint32_t packets_sent;
    uint32_t packets_received;
    uint32_t bytes_sent;
    uint32_t bytes_received;
    uint32_t errors;
} net_stats_t;

seqlock_t stats_lock = {0};
net_stats_t net_stats = {0};

// Called by ISR or network task (writer)
void network_packet_sent(uint32_t bytes)
{
    taskENTER_CRITICAL();
    stats_lock.sequence++;
    
    net_stats.packets_sent++;
    net_stats.bytes_sent += bytes;
    
    stats_lock.sequence++;
    taskEXIT_CRITICAL();
}

// Called by monitoring task (reader - frequent)
void get_network_stats(net_stats_t *out)
{
    uint32_t seq;
    do {
        do {
            seq = stats_lock.sequence;
        } while (seq & 1);
        
        *out = net_stats;  // Struct copy
        
    } while (stats_lock.sequence != seq);
}
```

---

## 3. RTIC (Rust) Implementation

RTIC provides compile-time guarantees about resource access. While it has its own lock-free mechanisms, we can implement seqlock patterns for specific use cases.

```rust
#[rtic::app(device = stm32f4xx_hal::pac, peripherals = true)]
mod app {
    use core::sync::atomic::{AtomicU32, Ordering};
    
    // Shared data protected by seqlock
    #[shared]
    struct Shared {
        sequence: AtomicU32,
        sensor_data: SensorData,
    }
    
    #[local]
    struct Local {}
    
    #[derive(Clone, Copy)]
    struct SensorData {
        temperature: i32,
        pressure: i32,
        timestamp: u32,
    }
    
    #[init]
    fn init(ctx: init::Context) -> (Shared, Local) {
        // Setup hardware...
        
        (
            Shared {
                sequence: AtomicU32::new(0),
                sensor_data: SensorData {
                    temperature: 0,
                    pressure: 0,
                    timestamp: 0,
                },
            },
            Local {},
        )
    }
    
    // High-priority reader (runs frequently)
    #[task(shared = [sequence, sensor_data], priority = 2)]
    async fn display_task(ctx: display_task::Context) {
        let sequence = ctx.shared.sequence;
        let sensor_data = ctx.shared.sensor_data;
        
        loop {
            // Seqlock read
            let data = seqlock_read(sequence, sensor_data);
            
            // Display data
            defmt::info!("Temp: {}, Pressure: {}", 
                        data.temperature, data.pressure);
            
            Systick::delay(10.millis()).await;
        }
    }
    
    // Low-priority writer (updates occasionally)
    #[task(shared = [sequence, sensor_data], priority = 1)]
    async fn sensor_task(ctx: sensor_task::Context) {
        let sequence = ctx.shared.sequence;
        let sensor_data = ctx.shared.sensor_data;
        
        loop {
            // Read sensors
            let new_temp = read_temperature();
            let new_pressure = read_pressure();
            
            // Seqlock write
            seqlock_write(sequence, sensor_data, |data| {
                data.temperature = new_temp;
                data.pressure = new_pressure;
                data.timestamp = get_timestamp();
            });
            
            Systick::delay(1000.millis()).await;
        }
    }
}

// Seqlock helper functions
fn seqlock_read<T: Copy>(
    sequence: &AtomicU32, 
    data: &T
) -> T {
    loop {
        // Read sequence (must be even)
        let seq = loop {
            let s = sequence.load(Ordering::Acquire);
            if s & 1 == 0 { break s; }
            core::hint::spin_loop();
        };
        
        // Read data
        let copy = *data;
        
        compiler_fence(Ordering::Acquire);
        
        // Verify sequence unchanged
        if sequence.load(Ordering::Acquire) == seq {
            return copy;
        }
        // Retry if sequence changed
    }
}

fn seqlock_write<T, F>(
    sequence: &AtomicU32,
    data: &mut T,
    update: F
) where F: FnOnce(&mut T) {
    // Increment sequence (make odd)
    let old_seq = sequence.fetch_add(1, Ordering::Release);
    
    compiler_fence(Ordering::Release);
    
    // Update data
    update(data);
    
    compiler_fence(Ordering::Release);
    
    // Increment sequence (make even)
    sequence.store(old_seq + 2, Ordering::Release);
}
```

### RTIC Example: Motor Control Data

```rust
// Motor controller that frequently reads position/velocity
// but infrequently updates from encoder readings

#[derive(Clone, Copy)]
struct MotorState {
    position: i32,      // Encoder counts
    velocity: i32,      // Counts per second
    current: i16,       // Motor current in mA
    target_position: i32,
}

// High-frequency control loop (10kHz)
#[task(shared = [sequence, motor_state], priority = 3)]
async fn control_loop(ctx: control_loop::Context) {
    loop {
        // Fast, lock-free read
        let state = seqlock_read(
            ctx.shared.sequence, 
            ctx.shared.motor_state
        );
        
        // PID control calculations
        let error = state.target_position - state.position;
        let control_output = pid_calculate(error, state.velocity);
        
        set_motor_pwm(control_output);
        
        Systick::delay(100.micros()).await;  // 10kHz
    }
}

// Lower-frequency encoder update (1kHz)
#[task(shared = [sequence, motor_state], priority = 2)]
async fn encoder_task(ctx: encoder_task::Context) {
    loop {
        let encoder_pos = read_encoder();
        let encoder_vel = calculate_velocity();
        let motor_current = read_current_sensor();
        
        seqlock_write(
            ctx.shared.sequence,
            ctx.shared.motor_state,
            |state| {
                state.position = encoder_pos;
                state.velocity = encoder_vel;
                state.current = motor_current;
            }
        );
        
        Systick::delay(1.millis()).await;  // 1kHz
    }
}
```

---

## 4. Embassy (Async Rust) Implementation

Embassy uses async/await for embedded systems. Seqlocks work well here for sharing data between async tasks and interrupt handlers.

```rust
use embassy_executor::Spawner;
use embassy_sync::blocking_mutex::raw::CriticalSectionRawMutex;
use embassy_time::{Duration, Timer};
use core::sync::atomic::{AtomicU32, Ordering};

// Seqlock-protected data
struct SeqLock<T> {
    sequence: AtomicU32,
    data: critical_section::Mutex<T>,
}

impl<T: Copy> SeqLock<T> {
    pub const fn new(data: T) -> Self {
        Self {
            sequence: AtomicU32::new(0),
            data: critical_section::Mutex::new(data),
        }
    }
    
    pub fn read(&self) -> T {
        loop {
            // Wait for even sequence
            let seq = loop {
                let s = self.sequence.load(Ordering::Acquire);
                if s & 1 == 0 { break s; }
                core::hint::spin_loop();
            };
            
            // Read data in critical section (fast)
            let value = critical_section::with(|cs| {
                *self.data.borrow(cs)
            });
            
            // Check if sequence changed
            if self.sequence.load(Ordering::Acquire) == seq {
                return value;
            }
        }
    }
    
    pub fn write<F>(&self, f: F)
    where F: FnOnce(&mut T) {
        critical_section::with(|cs| {
            // Start write (odd sequence)
            self.sequence.fetch_add(1, Ordering::Release);
            
            // Update data
            f(&mut self.data.borrow(cs));
            
            // End write (even sequence)
            self.sequence.fetch_add(1, Ordering::Release);
        });
    }
}

#[derive(Clone, Copy)]
struct SensorReading {
    accel_x: i16,
    accel_y: i16,
    accel_z: i16,
    gyro_x: i16,
    gyro_y: i16,
    gyro_z: i16,
    timestamp_us: u64,
}

static IMU_DATA: SeqLock<SensorReading> = SeqLock::new(
    SensorReading {
        accel_x: 0, accel_y: 0, accel_z: 0,
        gyro_x: 0, gyro_y: 0, gyro_z: 0,
        timestamp_us: 0,
    }
);

#[embassy_executor::main]
async fn main(spawner: Spawner) {
    // Initialize hardware...
    
    spawner.spawn(imu_reader_task()).unwrap();
    spawner.spawn(sensor_fusion_task()).unwrap();
    spawner.spawn(display_task()).unwrap();
}

// Writer: Reads IMU at 1kHz (interrupt-driven)
#[embassy_executor::task]
async fn imu_reader_task() {
    let mut imu = setup_imu_sensor();
    
    loop {
        // Wait for data ready from IMU
        imu.wait_for_data().await;
        
        // Read sensor (fast operation)
        let accel = imu.read_accelerometer();
        let gyro = imu.read_gyroscope();
        let timestamp = embassy_time::Instant::now().as_micros();
        
        // Write with seqlock (non-blocking for readers)
        IMU_DATA.write(|data| {
            data.accel_x = accel.x;
            data.accel_y = accel.y;
            data.accel_z = accel.z;
            data.gyro_x = gyro.x;
            data.gyro_y = gyro.y;
            data.gyro_z = gyro.z;
            data.timestamp_us = timestamp;
        });
        
        Timer::after(Duration::from_millis(1)).await;
    }
}

// Reader: Sensor fusion at 100Hz
#[embassy_executor::task]
async fn sensor_fusion_task() {
    let mut kalman_filter = KalmanFilter::new();
    
    loop {
        // Lock-free read (no waiting for writer)
        let imu = IMU_DATA.read();
        
        // Perform sensor fusion calculations
        kalman_filter.update(
            imu.accel_x, imu.accel_y, imu.accel_z,
            imu.gyro_x, imu.gyro_y, imu.gyro_z
        );
        
        let orientation = kalman_filter.get_orientation();
        
        // Use orientation data...
        
        Timer::after(Duration::from_millis(10)).await;
    }
}

// Reader: Display update at 10Hz
#[embassy_executor::task]
async fn display_task() {
    loop {
        // Quick, non-blocking read
        let imu = IMU_DATA.read();
        
        defmt::info!(
            "Accel: ({}, {}, {}) Gyro: ({}, {}, {})",
            imu.accel_x, imu.accel_y, imu.accel_z,
            imu.gyro_x, imu.gyro_y, imu.gyro_z
        );
        
        Timer::after(Duration::from_millis(100)).await;
    }
}
```

### Embassy Example: Wireless Telemetry

```rust
// Multiple readers (USB, BLE, logging) reading telemetry
// Single writer (sensor aggregator) updating infrequently

#[derive(Clone, Copy)]
struct TelemetryData {
    battery_voltage: u16,
    battery_current: i16,
    cpu_temperature: i16,
    uptime_seconds: u32,
    packet_count: u32,
}

static TELEMETRY: SeqLock<TelemetryData> = SeqLock::new(
    TelemetryData {
        battery_voltage: 0,
        battery_current: 0,
        cpu_temperature: 0,
        uptime_seconds: 0,
        packet_count: 0,
    }
);

// Writer updates every second
#[embassy_executor::task]
async fn telemetry_updater() {
    let mut uptime = 0u32;
    
    loop {
        let voltage = read_battery_voltage();
        let current = read_battery_current();
        let temp = read_cpu_temperature();
        
        TELEMETRY.write(|data| {
            data.battery_voltage = voltage;
            data.battery_current = current;
            data.cpu_temperature = temp;
            data.uptime_seconds = uptime;
        });
        
        uptime += 1;
        Timer::after(Duration::from_secs(1)).await;
    }
}

// Fast USB reader (100Hz)
#[embassy_executor::task]
async fn usb_telemetry_task(mut usb: UsbDevice) {
    loop {
        let data = TELEMETRY.read();  // Non-blocking
        
        send_telemetry_packet(&mut usb, data).await;
        
        Timer::after(Duration::from_millis(10)).await;
    }
}

// BLE reader (10Hz)
#[embassy_executor::task]
async fn ble_telemetry_task(mut ble: BlePeripheral) {
    loop {
        let data = TELEMETRY.read();  // Non-blocking
        
        ble.update_characteristics(data).await;
        
        Timer::after(Duration::from_millis(100)).await;
    }
}

// Logger reader (1Hz)
#[embassy_executor::task]
async fn log_telemetry_task() {
    loop {
        let data = TELEMETRY.read();  // Non-blocking
        
        defmt::info!(
            "Battery: {}mV @ {}mA, Temp: {}C, Uptime: {}s",
            data.battery_voltage,
            data.battery_current,
            data.cpu_temperature,
            data.uptime_seconds
        );
        
        Timer::after(Duration::from_secs(1)).await;
    }
}
```

---

## Performance Comparison

| Aspect | Seqlock | Mutex | RwLock |
|--------|---------|-------|---------|
| **Read speed** | Fastest (no locking) | Slow (locks) | Medium (tracks readers) |
| **Write speed** | Fast (simple increment) | Fast | Fast |
| **Reader blocking** | Never | Always | Only by writers |
| **Writer blocking** | By other writers only | Always | By readers & writers |
| **Best for** | Frequent reads, rare writes | Balanced read/write | More reads than writes |
| **Worst case** | Reader starvation (busy loop) | Contention | Writer starvation |

## When to Use Seqlocks

✅ **Good for:**
- Reading small data structures (cache-line sized)
- Read-to-write ratio > 10:1
- Time-critical readers that cannot block
- Data that tolerates brief inconsistency
- Statistics, counters, timestamps

❌ **Bad for:**
- Large data structures (retry cost too high)
- Equal or write-heavy workloads
- Data that must be transactionally consistent
- Situations where retry loops are unacceptable

The key insight is that seqlocks trade **write simplicity and reader lock-freedom** for **potential reader retries** — a worthwhile tradeoff when reads vastly outnumber writes.