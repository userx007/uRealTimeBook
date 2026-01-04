# Linux Kernel Completion Variables

## Overview

**Completion variables** are a Linux kernel synchronization primitive designed specifically for scenarios where one thread needs to signal another that a specific event has occurred. They provide a simple, efficient mechanism for "one-shot" event notification and are much simpler than condition variables for this specific use case.

## Key Characteristics

- **One-shot synchronization**: Designed for scenarios where you wait for something to complete once
- **Kernel-only**: Available only in kernel space, not user space
- **Simple API**: Easier to use correctly than condition variables or semaphores for event waiting
- **Race-free**: Built to avoid common race conditions in completion scenarios
- **Efficient**: Optimized for the specific pattern of waiting and completion

## Core Concepts

A completion has two states:
1. **Not done** (initial state) - waiters will block
2. **Done** - waiters proceed immediately

The basic pattern is:
- One or more threads **wait** for the completion
- Another thread **completes** it, waking the waiter(s)

## API Functions

### Initialization

```c
#include <linux/completion.h>

/* Static initialization */
DECLARE_COMPLETION(my_completion);

/* Dynamic initialization */
struct completion my_completion;
init_completion(&my_completion);

/* Initialize as already complete */
DECLARE_COMPLETION_ONSTACK(my_completion);
```

### Waiting Functions

```c
/* Wait indefinitely for completion */
void wait_for_completion(struct completion *c);

/* Wait with timeout (in jiffies) */
unsigned long wait_for_completion_timeout(struct completion *c, 
                                          unsigned long timeout);

/* Wait, but allow interruption by signals */
int wait_for_completion_interruptible(struct completion *c);

/* Combination: interruptible with timeout */
long wait_for_completion_interruptible_timeout(struct completion *c,
                                               unsigned long timeout);

/* Wait, but killable by fatal signals only */
int wait_for_completion_killable(struct completion *c);
```

### Signaling Functions

```c
/* Signal one waiter */
void complete(struct completion *c);

/* Signal all waiters */
void complete_all(struct completion *c);
```

### Reinitialization

```c
/* Reset completion to "not done" state for reuse */
void reinit_completion(struct completion *c);
```

## Example 1: Simple Thread Synchronization

```c
#include <linux/completion.h>
#include <linux/kthread.h>

static DECLARE_COMPLETION(thread_done);

static int worker_thread(void *data)
{
    printk(KERN_INFO "Worker: Starting work...\n");
    
    /* Simulate some work */
    msleep(2000);
    
    printk(KERN_INFO "Worker: Work complete!\n");
    
    /* Signal completion */
    complete(&thread_done);
    
    return 0;
}

static int start_and_wait(void)
{
    struct task_struct *thread;
    
    printk(KERN_INFO "Main: Starting worker thread\n");
    
    thread = kthread_run(worker_thread, NULL, "worker");
    if (IS_ERR(thread))
        return PTR_ERR(thread);
    
    printk(KERN_INFO "Main: Waiting for worker to complete...\n");
    
    /* Wait for the worker to finish */
    wait_for_completion(&thread_done);
    
    printk(KERN_INFO "Main: Worker has completed!\n");
    
    return 0;
}
```

## Example 2: Device Driver Waiting for Hardware

```c
struct my_device {
    struct completion irq_completion;
    void __iomem *registers;
    /* ... other fields ... */
};

/* Interrupt handler */
static irqreturn_t my_device_irq(int irq, void *dev_id)
{
    struct my_device *dev = dev_id;
    
    /* Read status register */
    u32 status = readl(dev->registers + STATUS_REG);
    
    if (status & OPERATION_COMPLETE) {
        /* Signal completion */
        complete(&dev->irq_completion);
        return IRQ_HANDLED;
    }
    
    return IRQ_NONE;
}

/* Function that starts operation and waits */
static int perform_device_operation(struct my_device *dev)
{
    int ret;
    
    /* Initialize completion */
    reinit_completion(&dev->irq_completion);
    
    /* Start hardware operation */
    writel(START_OPERATION, dev->registers + CONTROL_REG);
    
    /* Wait for interrupt to signal completion (with 5 second timeout) */
    ret = wait_for_completion_timeout(&dev->irq_completion, 
                                      msecs_to_jiffies(5000));
    
    if (ret == 0) {
        printk(KERN_ERR "Operation timed out\n");
        return -ETIMEDOUT;
    }
    
    printk(KERN_INFO "Operation completed successfully\n");
    return 0;
}
```

## Example 3: Kernel Module Init/Exit Synchronization

```c
static struct completion module_exit_completion;
static struct task_struct *background_task;

static int background_worker(void *unused)
{
    while (!kthread_should_stop()) {
        /* Do background work */
        printk(KERN_INFO "Working...\n");
        msleep(1000);
    }
    
    /* Signal that we've finished cleanup */
    complete(&module_exit_completion);
    return 0;
}

static int __init my_module_init(void)
{
    init_completion(&module_exit_completion);
    
    background_task = kthread_run(background_worker, NULL, "bg_worker");
    if (IS_ERR(background_task))
        return PTR_ERR(background_task);
    
    printk(KERN_INFO "Module loaded\n");
    return 0;
}

static void __exit my_module_exit(void)
{
    /* Request thread to stop */
    kthread_stop(background_task);
    
    /* Wait for thread to finish cleanup */
    wait_for_completion(&module_exit_completion);
    
    printk(KERN_INFO "Module unloaded\n");
}
```

## Example 4: Multiple Waiters with complete_all()

```c
static DECLARE_COMPLETION(start_signal);

static int worker_thread(void *id)
{
    int thread_id = *(int *)id;
    
    printk(KERN_INFO "Thread %d: Waiting for start signal\n", thread_id);
    
    /* All threads wait here */
    wait_for_completion(&start_signal);
    
    printk(KERN_INFO "Thread %d: Started!\n", thread_id);
    
    /* Do actual work */
    msleep(1000 * thread_id);
    
    return 0;
}

static int start_multiple_workers(void)
{
    int i, ids[5];
    
    for (i = 0; i < 5; i++) {
        ids[i] = i;
        kthread_run(worker_thread, &ids[i], "worker_%d", i);
    }
    
    msleep(500); /* Let threads start waiting */
    
    printk(KERN_INFO "Releasing all threads!\n");
    
    /* Wake all waiting threads at once */
    complete_all(&start_signal);
    
    return 0;
}
```

## Completions vs Other Synchronization Primitives

### vs Semaphores
- **Completions**: Designed for event signaling, simpler API, better for one-shot scenarios
- **Semaphores**: General-purpose counting, resource management, can be used multiple times

### vs Condition Variables
- **Completions**: Simpler, no explicit locking required, built for one-shot events
- **Condition Variables**: More complex, require explicit locking, better for complex predicates

### vs Wait Queues
- **Completions**: Higher-level abstraction, safer, easier to use correctly
- **Wait Queues**: Lower-level, more flexible but easier to misuse

## Common Use Cases

1. **Thread synchronization**: Wait for a thread to finish initialization or cleanup
2. **Interrupt handling**: Wait for hardware interrupts to signal operation completion
3. **Module loading/unloading**: Coordinate initialization and cleanup
4. **Device driver operations**: Wait for DMA transfers, I/O operations
5. **Deferred work**: Signal when bottom-half processing is complete
6. **Resource initialization**: Wait for subsystems to become ready

## Best Practices

1. **Use timeouts**: Always consider using timeout variants to avoid indefinite hangs
2. **Handle interruptions**: Use interruptible variants when appropriate
3. **Reinitialize for reuse**: Call `reinit_completion()` before reusing
4. **Choose the right function**: Use `complete_all()` only when you have multiple waiters
5. **Avoid holding locks**: Don't hold spinlocks while waiting for completion
6. **Memory barriers**: Completions include necessary memory barriers automatically

## Important Notes

- Completions are **NOT recursive** - completing multiple times has no additional effect (unless reinitialized)
- A completion can be used by multiple threads waiting, but typically used for 1:1 or 1:N scenarios
- The completion state persists until explicitly reinitialized with `reinit_completion()`
- Safe to use in interrupt context for `complete()` but never wait in interrupt context

Completions provide a clean, efficient way to handle event-based synchronization in the Linux kernel, eliminating many common pitfalls associated with manual wait queue management.