# Real-Time Synchronization Mechanisms

Here's a comprehensive overview of synchronization mechanisms used in real-time operating systems, organized by their general availability and specific implementations:

## Universal Mechanisms (Available Across Most RTOSes)

**Mutexes** - Basic mutual exclusion locks found in Linux (pthread_mutex, futex-based), FreeRTOS (xSemaphoreCreateMutex), and similar constructs in RTIC/Embassy (though implemented differently). Linux and FreeRTOS support priority inheritance to prevent priority inversion. Embassy uses Rust's async Mutex that works with its executor.

**Binary Semaphores** - Used for signaling between tasks/threads. Available in Linux (POSIX semaphores, System V semaphores), FreeRTOS (xSemaphoreCreateBinary), and as primitives in other systems. These differ from mutexes as they're meant for signaling rather than ownership.

**Counting Semaphores** - Track multiple resources. Found in Linux (sem_t with counts > 1), FreeRTOS (xSemaphoreCreateCounting), though less common in modern async frameworks like Embassy.

**Spinlocks** - Busy-waiting locks crucial for short critical sections. Linux has various types (spin_lock, spin_lock_irqsave for kernel), FreeRTOS uses taskENTER_CRITICAL/taskEXIT_CRITICAL for similar effects, RTIC uses critical sections based on interrupt masking, and Embassy provides CriticalSectionMutex.

## Linux-Specific Mechanisms

**Futex (Fast Userspace Mutex)** - Low-level synchronization primitive unique to Linux that most higher-level constructs are built upon. Combines userspace atomic operations with kernel-space waiting queues.

**RCU (Read-Copy-Update)** - Linux kernel's lock-free synchronization for read-heavy workloads. Allows multiple readers without locks while writers create new versions of data structures.

**Seqlocks** - Linux kernel mechanism allowing readers to detect concurrent writes and retry. Optimized for scenarios where reads are much more frequent than writes.

**Completion Variables** - Linux kernel-specific for waiting on events, simpler than condition variables for one-shot synchronization.

**RT-mutexes** - Real-time variant in Linux's PREEMPT_RT patches with priority inheritance and priority ceiling protocols.

## FreeRTOS-Specific Mechanisms

**Direct-to-Task Notifications** - Lightweight FreeRTOS mechanism (xTaskNotify/xTaskNotifyWait) that's faster than semaphores, allowing tasks to send 32-bit values directly to other tasks.

**Event Groups** - FreeRTOS-specific mechanism for synchronizing on multiple events using bit flags. Tasks can wait for any combination of bits to be set.

**Stream Buffers and Message Buffers** - FreeRTOS optimized data passing mechanisms for single-producer/single-consumer scenarios, particularly useful for interrupt-to-task communication.

## RTIC-Specific Mechanisms

**Resource Locking via RTFM Protocol** - RTIC's unique approach using Stack Resource Policy (SRP) where resources are protected by raising the priority ceiling rather than traditional locks. This provides deadlock-free guarantees at compile time.

**Software Tasks** - RTIC's message-passing mechanism where tasks communicate via queues with compile-time capacity checking.

**Lock-free resource access** - RTIC can prove at compile time when resources don't need locks because only one task can access them.

## Embassy-Specific Mechanisms

**Async Channels** - Embassy's message-passing primitives (embassy_sync::channel) that work with Rust's async/await, supporting both bounded and unbounded variants.

**Signals** - Embassy's lightweight notification mechanism (embassy_sync::signal::Signal) for waking async tasks.

**Watch/WatchBehavior** - Embassy primitives for broadcasting state changes to multiple subscribers.

**Async Mutex/RwLock** - Embassy provides async-aware locks that yield rather than block, integrating with the async executor.

## Advanced/Specialized Mechanisms

**Memory Barriers** - Hardware-level synchronization (found in Linux kernel programming with smp_mb(), smp_rmb(), smp_wmb()). FreeRTOS and bare-metal systems use architecture-specific barriers.

**Atomic Operations** - Lock-free primitives available across all systems. Linux uses atomic_t and atomic64_t, Rust (RTIC/Embassy) has std::sync::atomic types, FreeRTOS often relies on architecture-specific atomics or critical sections.

**Reader-Writer Locks** - Allow multiple readers or single writer. Linux has pthread_rwlock and kernel rwlock_t/rwsem. Less common in smaller RTOSes but Embassy provides async RwLock.

**Condition Variables** - For complex waiting conditions. Available in Linux (pthread_cond_t), less common in minimal RTOSes like FreeRTOS (though can be built from primitives), and Embassy has async equivalents.

**Barriers** - Synchronize groups of threads/tasks at a point. Linux has pthread_barrier_t, can be constructed in FreeRTOS from semaphores, less relevant in RTIC's task model.

The key distinction is that traditional RTOSes like FreeRTOS use blocking synchronization primitives, Linux supports both blocking and lock-free approaches depending on context (userspace vs kernel), while modern Rust frameworks like RTIC provide compile-time guarantees to eliminate many runtime locks entirely, and Embassy focuses on async/await patterns that avoid blocking altogether. RTIC in particular is notable for moving synchronization concerns from runtime to compile-time wherever possible.