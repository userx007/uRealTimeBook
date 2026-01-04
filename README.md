# uRealTimeBook

[1. **Real-Time Synchronization Mechanisms**](docs/01_real_time_synchronization_mechanisms.md)<br>
A comprehensive overview of synchronization mechanisms used in real-time operating systems, organized by their general availability and specific implementations: 



## Universal Mechanisms (Available Across Most RTOSes)

[2. **Semaphores:  Detailed Overview Across Platforms**](docs/02_semaphores.md)<br>
Understanding semaphores with examples in Linux, FreeRTOS, RTIC and Embassy 

[3. **Mutexes**](docs/03_mutexes.md)<br>
Basic mutual exclusion locks found in Linux (pthread_mutex, futex-based), FreeRTOS (xSemaphoreCreateMutex), and similar constructs in RTIC/Embassy (though implemented differently). Linux and FreeRTOS support priority inheritance to prevent priority inversion. Embassy uses Rust's async Mutex that works with its executor.

[4. **Binary Semaphores**](docs/04_binary_semaphores.md)<br>
Used for signaling between tasks/threads. Available in Linux (POSIX semaphores, System V semaphores), FreeRTOS (xSemaphoreCreateBinary), and as primitives in other systems. These differ from mutexes as they're meant for signaling rather than ownership.

[5. **Counting Semaphores: Track multiple resources**](docs/05_counting_semaphores.md)<br>
Track multiple resources. Found in Linux (sem_t with counts > 1), FreeRTOS (xSemaphoreCreateCounting), though less common in modern async frameworks like Embassy.

[6. **Spinlocks**](docs/06_spin_locks.md)<br>
Busy-waiting locks crucial for short critical sections. Linux has various types (spin_lock, spin_lock_irqsave for kernel), FreeRTOS uses taskENTER_CRITICAL/taskEXIT_CRITICAL for similar effects, RTIC uses critical sections based on interrupt masking, and Embassy provides CriticalSectionMutex



## Linux-Specific Mechanisms

[7. **Futex (Fast Userspace Mutex)**](docs/07_linux_futex.md)<br>
Low-level synchronization primitive unique to Linux that most higher-level constructs are built upon. Combines userspace atomic operations with kernel-space waiting queues.

[8. **RCU (Read-Copy-Update)**](docs/08_linux_rcu.md)<br>
Linux kernel's lock-free synchronization for read-heavy workloads. Allows multiple readers without locks while writers create new versions of data structures

[9. **Seqlocks**](docs/09_linux_seqlocks.md)<br>
Linux kernel mechanism allowing readers to detect concurrent writes and retry. Optimized for scenarios where reads are much more frequent than writes.

[10. **Completion Variables**](docs/10_linux_completion_variables.md)<br>
Linux kernel-specific for waiting on events, simpler than condition variables for one-shot synchronization.

[11. **RT-mutexes**](docs/11_linux_rt_mutex.md)<br>
Real-time variant in Linux's PREEMPT_RT patches with priority inheritance and priority ceiling protocols.



## FreeRTOS-Specific Mechanisms

[12. **Direct-to-Task Notifications**](docs/12_freertos_direct_to_task_notifications.md)<br>
Lightweight FreeRTOS mechanism (xTaskNotify/xTaskNotifyWait) that's faster than semaphores, allowing tasks to send 32-bit values directly to other tasks.

[13. **Event Groups**](docs/13_freertos_event_gropus.md)<br>
FreeRTOS-specific mechanism for synchronizing on multiple events using bit flags. Tasks can wait for any combination of bits to be set.

[14. **Stream Buffers and Message Buffers**](docs/14_freertos_stream_message_buffers.md)<br>
FreeRTOS optimized data passing mechanisms for single-producer/single-consumer scenarios, particularly useful for interrupt-to-task communication.



## RTIC-Specific Mechanisms

[13. **Event Groups**](docs/05_counting_semaphores.md)<br>

## Embassy-Specific Mechanisms

[14. **Stream Buffers and Message Buffers**](docs/05_counting_semaphores.md)<br>

## Advanced/Specialized Mechanisms

[6. ** **](docs/05_counting_semaphores.md)<br>