# FreeRTOS Direct-to-Task Notifications

Direct-to-Task Notifications are a lightweight inter-task communication mechanism introduced in FreeRTOS v8.2.0. They provide a fast, memory-efficient alternative to traditional synchronization primitives like semaphores, queues, and event groups for many common RTOS scenarios.

## Core Concept

Each FreeRTOS task has a built-in 32-bit notification value and a notification state (pending/not-pending). Tasks can send notifications directly to other tasks without requiring a separate kernel object, making this mechanism extremely efficient in terms of both speed and RAM usage.

## Key Characteristics

**Performance**: Task notifications are approximately 45% faster and use significantly less RAM than binary semaphores because they don't require a separate kernel object to be created and maintained.

**Notification Value**: The 32-bit value can be used in multiple ways:
- As a simple event flag
- To pass data (limited to 32 bits)
- As a counting semaphore
- As a binary semaphore

**Notification State**: Tracks whether a notification is pending, similar to how a semaphore tracks availability.

## API Functions

**Sending notifications:**
- `xTaskNotify()` / `xTaskNotifyFromISR()` - Send notification and update value
- `xTaskNotifyGive()` / `vTaskNotifyGiveFromISR()` - Increment notification value (semaphore-like)

**Receiving notifications:**
- `xTaskNotifyWait()` - Wait with optional bit manipulation
- `ulTaskNotifyTake()` - Wait and decrement (semaphore-like)

## Practical Examples

### Example 1: Simple Binary Semaphore Replacement

```c
// Task handle for the receiving task
TaskHandle_t xReceivingTask;

// Producer task (replaces semaphore give)
void vProducerTask(void *pvParameters)
{
    while(1)
    {
        // Do some work...
        process_data();
        
        // Notify the consumer task (like giving a semaphore)
        xTaskNotifyGive(xReceivingTask);
        
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

// Consumer task (replaces semaphore take)
void vConsumerTask(void *pvParameters)
{
    while(1)
    {
        // Wait for notification (like taking a semaphore)
        // Block indefinitely until notified
        ulTaskNotifyTake(pdTRUE,      // Clear notification on exit
                        portMAX_DELAY); // Wait indefinitely
        
        // Process the notification
        handle_event();
    }
}

// Task creation
void setup_tasks(void)
{
    xTaskCreate(vConsumerTask, "Consumer", 128, NULL, 1, &xReceivingTask);
    xTaskCreate(vProducerTask, "Producer", 128, NULL, 1, NULL);
}
```

### Example 2: Passing Data with Notifications

```c
TaskHandle_t xDataProcessingTask;

// ISR that sends data via notification
void vUARTInterruptHandler(void)
{
    uint32_t received_byte = read_uart_data_register();
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;
    
    // Send the byte value as notification
    xTaskNotifyFromISR(xDataProcessingTask,
                       received_byte,
                       eSetValueWithOverwrite,
                       &xHigherPriorityTaskWoken);
    
    portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
}

// Task that receives and processes the data
void vDataProcessingTask(void *pvParameters)
{
    uint32_t received_value;
    
    while(1)
    {
        // Wait for notification with data
        xTaskNotifyWait(0x00,              // Don't clear bits on entry
                       0xFFFFFFFF,         // Clear all bits on exit
                       &received_value,    // Receive the value
                       portMAX_DELAY);     // Wait indefinitely
        
        // Process the received byte
        printf("Received: 0x%02X\n", received_value);
        process_byte(received_value);
    }
}
```

### Example 3: Event Flags Using Bit Manipulation

```c
// Define event bit flags
#define EVENT_SENSOR_DATA_READY  (1 << 0)
#define EVENT_BUTTON_PRESSED     (1 << 1)
#define EVENT_TIMEOUT_OCCURRED   (1 << 2)

TaskHandle_t xControlTask;

// Multiple sources setting different event flags
void vSensorTask(void *pvParameters)
{
    while(1)
    {
        read_sensor();
        // Set sensor ready bit
        xTaskNotify(xControlTask, 
                   EVENT_SENSOR_DATA_READY, 
                   eSetBits);
        vTaskDelay(pdMS_TO_TICKS(50));
    }
}

void vButtonTask(void *pvParameters)
{
    while(1)
    {
        if(button_pressed())
        {
            // Set button pressed bit
            xTaskNotify(xControlTask, 
                       EVENT_BUTTON_PRESSED, 
                       eSetBits);
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

// Control task waiting for events
void vControlTask(void *pvParameters)
{
    uint32_t notification_value;
    
    while(1)
    {
        // Wait for any event bit to be set
        xTaskNotifyWait(0x00,                    // Don't clear on entry
                       0xFFFFFFFF,               // Clear all on exit
                       &notification_value,      // Get value
                       pdMS_TO_TICKS(1000));    // Timeout after 1 second
        
        // Check which events occurred
        if(notification_value & EVENT_SENSOR_DATA_READY)
        {
            handle_sensor_data();
        }
        
        if(notification_value & EVENT_BUTTON_PRESSED)
        {
            handle_button_press();
        }
        
        if(notification_value == 0)
        {
            // Timeout occurred
            handle_timeout();
        }
    }
}
```

### Example 4: Counting Semaphore Behavior

```c
TaskHandle_t xWorkerTask;

// Producer incrementing the count
void vProducerTask(void *pvParameters)
{
    while(1)
    {
        // Generate work items
        if(work_available())
        {
            // Increment notification value (like giving to counting semaphore)
            xTaskNotifyGive(xWorkerTask);
        }
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}

// Consumer processing work items
void vWorkerTask(void *pvParameters)
{
    while(1)
    {
        // Wait for work (decrement count)
        ulTaskNotifyTake(pdFALSE,        // Don't clear, just decrement
                        portMAX_DELAY);   // Wait indefinitely
        
        // Process one work item
        process_work_item();
    }
}
```

## Action Modes for xTaskNotify()

When sending notifications, you can specify how the notification value should be updated:

- **eNoAction**: Just mark notification as pending, don't update value
- **eSetBits**: Bitwise OR the value (for event flags)
- **eIncrement**: Increment the value (counting semaphore)
- **eSetValueWithOverwrite**: Overwrite value unconditionally
- **eSetValueWithoutOverwrite**: Set value only if no notification pending

## Advantages

1. **Speed**: Significantly faster than queues and semaphores
2. **Memory**: No additional RAM for kernel objects
3. **Simplicity**: Less code required for simple synchronization
4. **Built-in**: Every task has this capability automatically

## Limitations

1. **Single receiver**: Only the owning task can receive its notifications
2. **Single value**: Only one 32-bit notification value per task
3. **No queuing**: Cannot queue multiple distinct notifications (though the value can count)
4. **No broadcasting**: Cannot send to multiple tasks simultaneously
5. **No peek**: Cannot check notification without consuming it

## When to Use

**Good use cases:**
- Replacing binary semaphores for task-to-task or ISR-to-task signaling
- Simple event flags (up to 32 distinct events)
- Lightweight counting semaphores
- Passing small data values (≤32 bits)

**Not suitable for:**
- Multiple producers to single consumer with distinct data items (use queue)
- Broadcasting to multiple tasks (use event groups)
- Complex synchronization requiring multiple kernel objects
- When you need to send more than 32 bits of data

Direct-to-Task Notifications are a powerful optimization for resource-constrained embedded systems, but should be chosen based on the specific requirements of your inter-task communication needs.