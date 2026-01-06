# uRealTimeBook


## Generics

[0. **Processes, Threads, and Tasks in Real-Time Programming**](docs/00_proc_thread_task.md)<br>
Definitions, analogies, examples

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

[15. **Resource Locking via RTFM Protocol**](docs/15_rtic_resource_locking.md)<br>
RTIC's unique approach using Stack Resource Policy (SRP) where resources are protected by raising the priority ceiling rather than traditional locks. This provides deadlock-free guarantees at compile time.

[16. **Software Tasks**](docs/16_rtic_software_tasks.md)<br>
RTIC's message-passing mechanism where tasks communicate via queues with compile-time capacity checking.

[17. **Lock-free resource access**](docs/17_rtic_lock_free_resource_access.md)<br>
RTIC can prove at compile time when resources don't need locks because only one task can access them.



## Embassy-Specific Mechanisms

[18. **Async Channels**](docs/18_embassy_async_channels.md)<br>
Embassy's message-passing primitives (embassy_sync::channel) that work with Rust's async/await, supporting both bounded and unbounded variants.

[19. **Signals**](docs/19_signals.md)<br>
Embassy's lightweight notification mechanism (embassy_sync::signal::Signal) for waking async tasks.

[20. **Watch/WatchBehavior**](docs/20_watch_watchbehavior.md)<br>
Embassy primitives for broadcasting state changes to multiple subscribers

[21. **Async Mutex/RwLock**](docs/21_async_mutex_rwlock.md)<br>
Embassy provides async-aware locks that yield rather than block, integrating with the async executor.



## Advanced/Specialized Mechanisms

[22. **Memory Barriers**](docs/22_memory_barriers.md)<br>
Hardware-level synchronization (found in Linux kernel programming with smp_mb(), smp_rmb(), smp_wmb()). FreeRTOS and bare-metal systems use architecture-specific barriers.

[23. **Atomic Operations**](docs/23_atomic_operations.md)<br>
Lock-free primitives available across all systems. Linux uses atomic_t and atomic64_t, Rust (RTIC/Embassy) has std::sync::atomic types, FreeRTOS often relies on architecture-specific atomics or critical sections.

[24. **Reader-Writer Locks**](docs/24_reader_writer_locks.md)<br>
Allow multiple readers or single writer. Linux has pthread_rwlock and kernel rwlock_t/rwsem. Less common in smaller RTOSes but Embassy provides async RwLock.

[25. **Condition Variables**](docs/25_condition_variables.md)<br>
For complex waiting conditions. Available in Linux (pthread_cond_t), less common in minimal RTOSes like FreeRTOS (though can be built from primitives), and Embassy has async equivalents.

[26. **Barriers**](docs/26_barriers.md)<br>
Synchronize groups of threads/tasks at a point. Linux has pthread_barrier_t, can be constructed in FreeRTOS from semaphores, less relevant in RTIC's task model.

[27. **Linux special system calls**](docs/27_linux_system_calls.md)<br>
Summary for the topics below

[28. **I/O Multiplexing in Linux**](docs/28_io_multiplexing_in_linux.md)<br>
epoll (epoll_create, epoll_ctl, epoll_wait), pselect, ppoll, io_uring (io_uring_setup, io_uring_enter)

[29. **Memory Mapping in Linux**](docs/29_memory_mapping_in_linux.md)<br>
munmap, mprotect, msync, mremap, madvise, mlock/munlock, mincore, memfd_create

[30. **Direct I/O and Zero-Copy in Linux**](docs/30_direct_io_and_zero_copy_in_linux.md)<br>
sendfile, splice, tee, vmsplice


[31. **Asynchronous I/O in Linux**](docs/31_asynchronous_io_in_linux.md)<br>
aio_read/aio_write, aio_suspend, io_submit/io_getevents

[32. **File Monitoring in Linux**](docs/32_file_monitoring_in_linux.md)<br>
inotify, fanotify