# FreeRTOS Stream Buffers and Message Buffers

## Overview

Stream Buffers and Message Buffers are lightweight, optimized data passing mechanisms introduced in FreeRTOS specifically for **single-producer/single-consumer** scenarios. They're designed to be fast, efficient, and particularly well-suited for **interrupt-to-task communication** or **task-to-task communication** where only one writer and one reader exist.

## Key Characteristics

**Lock-Free Operation**: These mechanisms use a lock-free implementation that avoids the overhead of critical sections or mutexes when used correctly (single producer, single consumer).

**Zero-Copy Possible**: Data can be written and read efficiently with minimal copying overhead.

**Interrupt-Safe**: Specifically designed with interrupt service routines (ISRs) in mind, with dedicated ISR-safe API functions.

**Memory Efficient**: Lower overhead compared to queues for streaming data scenarios.

---

## Stream Buffers

### What They Are
Stream Buffers pass a continuous stream of bytes from producer to consumer. They treat data as an unstructured byte stream without preserving message boundaries.

### When to Use
- Transferring UART/serial data from ISR to task
- Audio/video streaming data
- Sensor data streams
- Any scenario where you're moving raw byte streams

### Example: UART Reception

```c
#include "FreeRTOS.h"
#include "stream_buffer.h"

// Global stream buffer handle
StreamBufferHandle_t xUARTStreamBuffer;

// Buffer size
#define STREAM_BUFFER_SIZE 512

void setup_uart_stream(void) {
    // Create stream buffer (holds 512 bytes)
    xUARTStreamBuffer = xStreamBufferCreate(
        STREAM_BUFFER_SIZE,  // Total size
        1                     // Trigger level (bytes before unblocking)
    );
    
    if (xUARTStreamBuffer == NULL) {
        // Handle creation failure
    }
}

// UART Interrupt Service Routine
void UART_RX_IRQHandler(void) {
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;
    uint8_t receivedByte = UART_ReadByte();
    
    // Send from ISR - non-blocking
    xStreamBufferSendFromISR(
        xUARTStreamBuffer,
        &receivedByte,
        1,  // 1 byte
        &xHigherPriorityTaskWoken
    );
    
    // Yield if necessary
    portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
}

// Processing task
void vUARTProcessingTask(void *pvParameters) {
    uint8_t buffer[128];
    size_t bytesReceived;
    
    while (1) {
        // Block until data available (or timeout)
        bytesReceived = xStreamBufferReceive(
            xUARTStreamBuffer,
            buffer,
            sizeof(buffer),
            pdMS_TO_TICKS(1000)  // 1 second timeout
        );
        
        if (bytesReceived > 0) {
            // Process received data
            process_uart_data(buffer, bytesReceived);
        }
    }
}
```

---

## Message Buffers

### What They Are
Message Buffers are built on top of Stream Buffers but **preserve message boundaries**. Each write operation creates a discrete message that can be read as a complete unit.

### When to Use
- Passing complete packets or frames
- Variable-length messages with preserved boundaries
- Protocol frames (like CAN, Modbus frames)
- Command/response patterns

### Key Difference from Stream Buffers
Stream Buffer: `[A][B][C][D][E][F]` - continuous stream
Message Buffer: `[ABC][DEF]` - discrete messages with boundaries preserved

### Example: Sensor Data Packets

```c
#include "FreeRTOS.h"
#include "message_buffer.h"

// Message buffer handle
MessageBufferHandle_t xSensorMessageBuffer;

#define MESSAGE_BUFFER_SIZE 1024

// Sensor data structure
typedef struct {
    uint32_t timestamp;
    uint16_t sensorID;
    float temperature;
    float humidity;
} SensorData_t;

void setup_sensor_messaging(void) {
    xSensorMessageBuffer = xMessageBufferCreate(MESSAGE_BUFFER_SIZE);
    
    if (xSensorMessageBuffer == NULL) {
        // Handle error
    }
}

// Timer ISR that reads sensor
void TIMER_ISR(void) {
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;
    SensorData_t sensorData;
    
    // Read sensor (simplified)
    sensorData.timestamp = get_timestamp();
    sensorData.sensorID = 1;
    sensorData.temperature = read_temperature();
    sensorData.humidity = read_humidity();
    
    // Send complete message from ISR
    xMessageBufferSendFromISR(
        xSensorMessageBuffer,
        &sensorData,
        sizeof(SensorData_t),
        &xHigherPriorityTaskWoken
    );
    
    portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
}

// Data logging task
void vDataLoggingTask(void *pvParameters) {
    SensorData_t receivedData;
    size_t bytesReceived;
    
    while (1) {
        // Receive complete message (blocks until available)
        bytesReceived = xMessageBufferReceive(
            xSensorMessageBuffer,
            &receivedData,
            sizeof(SensorData_t),
            portMAX_DELAY
        );
        
        if (bytesReceived == sizeof(SensorData_t)) {
            // Process complete sensor reading
            log_sensor_data(&receivedData);
        }
    }
}
```

---

## Comparison with Queues

| Feature | Queue | Stream/Message Buffer |
|---------|-------|----------------------|
| **Producers** | Multiple | Single only |
| **Consumers** | Multiple | Single only |
| **Overhead** | Higher | Lower |
| **Use Case** | General purpose | Optimized streaming |
| **Variable Size** | Fixed item size | Variable (Message Buffer) |

### Example: Why Stream Buffer Over Queue

```c
// Using Queue - higher overhead
QueueHandle_t xByteQueue = xQueueCreate(512, sizeof(uint8_t));
// Each byte requires queue overhead

// Using Stream Buffer - more efficient
StreamBufferHandle_t xByteStream = xStreamBufferCreate(512, 1);
// Direct byte stream, less overhead
```

---

## Advanced Example: DMA + Stream Buffer

```c
// DMA completion callback
void DMA_TransferComplete_Callback(void) {
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;
    
    // DMA has filled buffer with ADC samples
    uint16_t adc_samples[256];
    
    xStreamBufferSendFromISR(
        xADCStreamBuffer,
        adc_samples,
        sizeof(adc_samples),
        &xHigherPriorityTaskWoken
    );
    
    portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
}

// DSP processing task
void vDSPTask(void *pvParameters) {
    uint16_t sample_buffer[256];
    size_t samples_received;
    
    while (1) {
        samples_received = xStreamBufferReceive(
            xADCStreamBuffer,
            sample_buffer,
            sizeof(sample_buffer),
            portMAX_DELAY
        );
        
        // Process samples with DSP algorithms
        perform_fft(sample_buffer, samples_received / sizeof(uint16_t));
    }
}
```

---

## Important Considerations

**Single Producer/Consumer Only**: Using multiple producers or consumers violates the design assumptions and can cause data corruption.

**Trigger Level**: The trigger level determines how many bytes must be in the buffer before a blocked reader unblocks.

**Space Overhead**: Message Buffers add 4 bytes per message for length information.

**Real-Time Performance**: These mechanisms are deterministic and suitable for hard real-time applications when used correctly.

**Not Suitable For**: Multi-producer/multi-consumer scenarios (use queues instead), situations requiring mutual exclusion (use mutexes/semaphores).

These mechanisms represent FreeRTOS's commitment to providing optimized, purpose-built tools for common embedded systems patterns, particularly the extremely common interrupt-to-task communication scenario.