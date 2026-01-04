# RT-Mutexes in Linux PREEMPT_RT

RT-mutexes (Real-Time mutexes) are a specialized locking mechanism introduced in Linux's PREEMPT_RT patches to address priority inversion problems in real-time systems. They provide deterministic locking behavior essential for meeting hard real-time deadlines.

## Background and Motivation

In traditional Linux, standard mutexes can cause **priority inversion** - a situation where a high-priority task is blocked waiting for a resource held by a low-priority task, while that low-priority task is preempted by medium-priority tasks. This makes timing guarantees impossible in real-time systems.

**Classic Priority Inversion Example:**
- Task H (high priority) needs a lock
- Task L (low priority) holds the lock
- Task M (medium priority) preempts Task L
- Result: Task H waits indefinitely while Task M runs, even though H has higher priority than M

## RT-Mutex Features

### 1. Priority Inheritance Protocol (PI)

When a high-priority task blocks on an RT-mutex held by a lower-priority task, the lower-priority task **temporarily inherits** the higher priority of the blocked task.

**How it works:**
```
Initial state:
- Task L (priority 10) holds mutex
- Task H (priority 90) blocks on mutex

After PI:
- Task L boosted to priority 90
- Task L runs immediately, finishes critical section
- Task L releases mutex and returns to priority 10
- Task H acquires mutex
```

### 2. Priority Ceiling Protocol

RT-mutexes can also implement priority ceiling, where each mutex has a ceiling priority equal to the highest priority of any task that might lock it. When a task locks the mutex, it's boosted to the ceiling priority.

## Code Examples

### Basic RT-Mutex Usage

```c
#include <linux/rtmutex.h>

struct rt_mutex my_lock;

/* Initialization */
void init_function(void)
{
    rt_mutex_init(&my_lock);
}

/* Locking and unlocking */
void critical_section(void)
{
    /* Acquire the RT-mutex */
    rt_mutex_lock(&my_lock);
    
    /* Critical section - protected code */
    // Perform time-critical operations
    
    /* Release the RT-mutex */
    rt_mutex_unlock(&my_lock);
}
```

### Priority Inheritance in Action

```c
#include <linux/kthread.h>
#include <linux/rtmutex.h>

static struct rt_mutex shared_lock;
static int shared_data = 0;

/* Low priority thread */
static int low_prio_thread(void *data)
{
    struct sched_param param = { .sched_priority = 10 };
    sched_setscheduler(current, SCHED_FIFO, &param);
    
    printk("Low priority thread acquiring lock\n");
    rt_mutex_lock(&shared_lock);
    
    /* Simulate work in critical section */
    msleep(1000);
    shared_data++;
    
    printk("Low priority thread releasing lock\n");
    rt_mutex_unlock(&shared_lock);
    
    return 0;
}

/* High priority thread */
static int high_prio_thread(void *data)
{
    struct sched_param param = { .sched_priority = 90 };
    sched_setscheduler(current, SCHED_FIFO, &param);
    
    msleep(100); /* Let low priority thread acquire lock first */
    
    printk("High priority thread trying to acquire lock\n");
    /* This will cause low_prio_thread to inherit priority 90 */
    rt_mutex_lock(&shared_lock);
    
    shared_data++;
    
    printk("High priority thread got the lock\n");
    rt_mutex_unlock(&shared_lock);
    
    return 0;
}
```

### Trylock with Timeout

```c
#include <linux/rtmutex.h>
#include <linux/hrtimer.h>

int timed_lock_example(void)
{
    struct hrtimer_sleeper timeout;
    ktime_t expires;
    int ret;
    
    /* Try to lock with timeout */
    expires = ktime_set(0, 100000000); /* 100ms timeout */
    
    ret = rt_mutex_timed_lock(&my_lock, &timeout);
    
    if (ret == 0) {
        /* Successfully acquired lock */
        // Do work
        rt_mutex_unlock(&my_lock);
        return 0;
    } else if (ret == -ETIMEDOUT) {
        printk("Lock acquisition timed out\n");
        return -1;
    }
    
    return ret;
}
```

## Implementation Details

### Internal Structure

RT-mutexes maintain a **priority-sorted wait queue** (using an rb-tree) of blocked tasks:

```c
struct rt_mutex {
    raw_spinlock_t wait_lock;
    struct rb_root_cached waiters;  /* Priority-sorted tree */
    struct task_struct *owner;
    /* ... */
};
```

### Priority Chain Walking

When priority inheritance occurs, the kernel must walk the **chain of blocked tasks**:

```
Task A (prio 90) blocks on Lock1 held by Task B (prio 50)
Task B (prio 50) blocks on Lock2 held by Task C (prio 10)

Result:
- Task C boosted to priority 90
- Task B boosted to priority 90
- Chain: A → Lock1 → B → Lock2 → C
```

## Comparison: Regular Mutex vs RT-Mutex

| Feature | Regular Mutex | RT-Mutex |
|---------|--------------|----------|
| Priority Inheritance | No | Yes |
| Bounded latency | No | Yes |
| Owner tracking | Basic | Full |
| Wait queue | Unordered | Priority-ordered |
| Overhead | Lower | Higher |
| Use case | General purpose | Real-time systems |

## Real-World Example: Device Driver

```c
#include <linux/module.h>
#include <linux/rtmutex.h>
#include <linux/interrupt.h>

struct rt_device {
    struct rt_mutex hw_lock;
    void __iomem *registers;
    int irq;
};

/* IRQ handler running at high priority */
static irqreturn_t rt_device_irq(int irq, void *dev_id)
{
    struct rt_device *dev = dev_id;
    
    /* Need to access hardware - will boost any holder's priority */
    rt_mutex_lock(&dev->hw_lock);
    
    /* Read/write hardware registers */
    iowrite32(STATUS_ACK, dev->registers + STATUS_REG);
    
    rt_mutex_unlock(&dev->hw_lock);
    
    return IRQ_HANDLED;
}

/* User context operation */
static ssize_t rt_device_write(struct file *file, const char __user *buf,
                               size_t count, loff_t *ppos)
{
    struct rt_device *dev = file->private_data;
    
    rt_mutex_lock(&dev->hw_lock);
    
    /* Configure hardware - if IRQ occurs, this task gets boosted */
    iowrite32(config_value, dev->registers + CONFIG_REG);
    
    rt_mutex_unlock(&dev->hw_lock);
    
    return count;
}
```

## PREEMPT_RT Patch Context

In the PREEMPT_RT patch, many traditional spinlocks are converted to RT-mutexes because:

1. **Spinlocks disable preemption** - bad for real-time latency
2. **RT-mutexes allow preemption** - locked task can sleep
3. **Priority inheritance prevents inversion**

```c
/* In standard kernel */
spinlock_t lock;
spin_lock(&lock);
/* Preemption disabled here */
spin_unlock(&lock);

/* In PREEMPT_RT kernel, spinlock_t becomes rt_mutex internally */
spinlock_t lock;  /* Actually an rt_mutex under the hood */
spin_lock(&lock);  /* Can sleep, allows preemption */
spin_unlock(&lock);
```

## Best Practices

1. **Keep critical sections short** - even with PI, long locks hurt determinism
2. **Avoid nested locking** - complicates priority chain walking
3. **Use consistent locking order** - prevents deadlocks
4. **Profile lock contention** - use `/proc/sys/kernel/hung_task_timeout_secs`
5. **Test with worst-case scenarios** - highest priority tasks blocking

## Debugging RT-Mutex Issues

```bash
# Enable RT-mutex debugging
echo 1 > /proc/sys/kernel/hung_task_warnings

# Check current RT priorities
chrt -p <pid>

# Trace lock contention
trace-cmd record -e lock:* -p function

# View priority inheritance in action
cat /proc/<pid>/sched | grep prio
```

RT-mutexes are fundamental to achieving deterministic behavior in Linux real-time systems, ensuring that high-priority tasks can meet their deadlines even in complex locking scenarios.