# FreeRTOS Event Groups - Detailed Description

## Overview

Event Groups are a FreeRTOS synchronization primitive that allows tasks to wait for multiple events simultaneously using a set of binary flags (bits). Each event group consists of a collection of event bits, where each bit represents a different event or condition. Tasks can wait for any combination of these bits to become set, making event groups particularly useful for complex synchronization scenarios where multiple conditions must be met.

## Core Characteristics

An event group is essentially a set of binary flags (typically 24 bits are available for application use in most FreeRTOS implementations, with 8 bits reserved for internal use). Each bit represents a distinct event or condition. Unlike binary or counting semaphores that signal a single event, event groups enable tasks to synchronize on multiple events using logical operations (AND/OR conditions).

The key advantage is flexibility: a task can wait for **any** bit to be set (OR condition), **all** bits to be set (AND condition), or any custom combination. This eliminates the need for multiple semaphores and complex coordination logic when dealing with multiple concurrent conditions.

## How Event Groups Work

When a task or interrupt service routine (ISR) sets one or more bits in an event group, any tasks waiting on those bits are evaluated to see if their waiting conditions have been met. If the conditions are satisfied, the waiting task(s) are unblocked and can proceed.

Tasks specify their waiting conditions using two parameters: which bits to wait for, and whether to wait for ALL specified bits (AND) or ANY of the specified bits (OR). Additionally, tasks can choose whether the bits should be automatically cleared when the wait condition is satisfied or left set for other tasks.

## Practical Examples

**Example 1: System Initialization Synchronization**

Consider an embedded system that must wait for multiple subsystems to initialize before starting the main application. You might have initialization tasks for the network stack, file system, and sensor hardware.

```c
/* Define event bits */
#define NETWORK_READY_BIT   (1 << 0)  // Bit 0
#define FILESYSTEM_READY_BIT (1 << 1)  // Bit 1
#define SENSORS_READY_BIT   (1 << 2)  // Bit 2

EventGroupHandle_t systemEventGroup;

/* Network initialization task */
void networkInitTask(void *pvParameters) {
    // Perform network initialization...
    initializeNetwork();
    
    // Signal that network is ready
    xEventGroupSetBits(systemEventGroup, NETWORK_READY_BIT);
    
    vTaskDelete(NULL);
}

/* Main application task */
void mainAppTask(void *pvParameters) {
    EventBits_t uxBits;
    
    // Wait for ALL subsystems to be ready
    uxBits = xEventGroupWaitBits(
        systemEventGroup,
        NETWORK_READY_BIT | FILESYSTEM_READY_BIT | SENSORS_READY_BIT,
        pdFALSE,  // Don't clear bits on exit
        pdTRUE,   // Wait for ALL bits (AND condition)
        portMAX_DELAY  // Wait indefinitely
    );
    
    // All subsystems ready, start main application
    runMainApplication();
}
```

**Example 2: Multi-Source Data Collection**

Imagine a data acquisition system that collects data from three different sensors and must process the data only when all three sensors have provided new readings.

```c
#define SENSOR_A_DATA_BIT  (1 << 0)
#define SENSOR_B_DATA_BIT  (1 << 1)
#define SENSOR_C_DATA_BIT  (1 << 2)

EventGroupHandle_t dataEventGroup;

/* Sensor reading ISR or task */
void sensorAReadComplete(void) {
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;
    
    // Called when sensor A has new data
    xEventGroupSetBitsFromISR(
        dataEventGroup,
        SENSOR_A_DATA_BIT,
        &xHigherPriorityTaskWoken
    );
    
    portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
}

/* Data processing task */
void dataProcessingTask(void *pvParameters) {
    while(1) {
        // Wait for all three sensors to have data
        xEventGroupWaitBits(
            dataEventGroup,
            SENSOR_A_DATA_BIT | SENSOR_B_DATA_BIT | SENSOR_C_DATA_BIT,
            pdTRUE,   // Clear bits after wait satisfied
            pdTRUE,   // Wait for ALL bits
            portMAX_DELAY
        );
        
        // All sensor data available, process it
        processSensorData();
    }
}
```

**Example 3: User Interface Event Handling**

In a system with a user interface, you might want to respond to different user inputs or system events. An event group can elegantly handle waiting for any of several possible events.

```c
#define BUTTON_PRESSED_BIT  (1 << 0)
#define TIMEOUT_BIT         (1 << 1)
#define DATA_RECEIVED_BIT   (1 << 2)

EventGroupHandle_t uiEventGroup;

void uiControlTask(void *pvParameters) {
    EventBits_t uxBits;
    
    while(1) {
        // Wait for ANY event to occur (OR condition)
        uxBits = xEventGroupWaitBits(
            uiEventGroup,
            BUTTON_PRESSED_BIT | TIMEOUT_BIT | DATA_RECEIVED_BIT,
            pdTRUE,   // Clear bits on exit
            pdFALSE,  // Wait for ANY bit (OR condition)
            pdMS_TO_TICKS(5000)  // 5 second timeout
        );
        
        if (uxBits & BUTTON_PRESSED_BIT) {
            handleButtonPress();
        }
        if (uxBits & TIMEOUT_BIT) {
            handleTimeout();
        }
        if (uxBits & DATA_RECEIVED_BIT) {
            handleDataReceived();
        }
        if (uxBits == 0) {
            // Wait timed out, no bits were set
            handleIdleState();
        }
    }
}
```

## Common Use Cases

Event groups excel in scenarios such as coordinating startup sequences where multiple initialization tasks must complete before the system begins normal operation, implementing state machines where transitions depend on multiple conditions being met simultaneously, managing power modes where the system should only enter low-power states when all subsystems are idle, and handling communication protocols that require synchronization of multiple events like transmission complete, acknowledgment received, and timeout conditions.

The mechanism is particularly valuable when you need to avoid the complexity of managing multiple semaphores or when tasks need to make decisions based on combinations of events rather than individual events in isolation.