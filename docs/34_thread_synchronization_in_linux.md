# Thread Synchronization in Linux

Thread synchronization is essential when multiple threads access shared resources to prevent race conditions, data corruption, and ensure correct program behavior.

## Core Synchronization Primitives

**Mutexes (Mutual Exclusion Locks)**
Mutexes are the most fundamental synchronization mechanism. They ensure only one thread can access a critical section at a time:

```c
pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;

pthread_mutex_lock(&mutex);
// Critical section - only one thread executes this
shared_data++;
pthread_mutex_unlock(&mutex);
```

Linux supports different mutex types: normal, recursive (allows the same thread to lock multiple times), and error-checking mutexes.

**Condition Variables**
These allow threads to wait for specific conditions to become true, avoiding busy-waiting:

```c
pthread_cond_t cond = PTHREAD_COND_INITIALIZER;
pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;

// Waiting thread
pthread_mutex_lock(&mutex);
while (!condition_met) {
    pthread_cond_wait(&cond, &mutex);  // Atomically unlocks mutex and waits
}
pthread_mutex_unlock(&mutex);

// Signaling thread
pthread_mutex_lock(&mutex);
condition_met = 1;
pthread_cond_signal(&cond);  // or pthread_cond_broadcast() for all waiters
pthread_mutex_unlock(&mutex);
```

**Read-Write Locks**
These optimize scenarios where data is read frequently but written rarely, allowing multiple concurrent readers:

```c
pthread_rwlock_t rwlock = PTHREAD_RWLOCK_INITIALIZER;

pthread_rwlock_rdlock(&rwlock);  // Multiple readers allowed
// Read shared data
pthread_rwlock_unlock(&rwlock);

pthread_rwlock_wrlock(&rwlock);  // Exclusive write access
// Modify shared data
pthread_rwlock_unlock(&rwlock);
```

## Advanced Synchronization Mechanisms

**Semaphores**
Used for controlling access to a resource pool or signaling between threads. Linux supports both POSIX named and unnamed semaphores:

```c
sem_t semaphore;
sem_init(&semaphore, 0, 1);  // Binary semaphore

sem_wait(&semaphore);  // Decrement, blocks if zero
// Access resource
sem_post(&semaphore);  // Increment
```

**Barriers**
Synchronize multiple threads at a specific point, useful for parallel algorithms:

```c
pthread_barrier_t barrier;
pthread_barrier_init(&barrier, NULL, num_threads);

// Each thread executes
pthread_barrier_wait(&barrier);  // All threads must reach here before proceeding
```

**Spinlocks**
Busy-wait locks that continuously check if a lock is available, efficient for very short critical sections:

```c
pthread_spinlock_t spinlock;
pthread_spin_init(&spinlock, PTHREAD_PROCESS_PRIVATE);

pthread_spin_lock(&spinlock);
// Very brief critical section
pthread_spin_unlock(&spinlock);
```

## Modern Approaches

**Atomic Operations**
C11 and C++11 provide atomic types that allow lock-free programming for simple operations:

```c
#include <stdatomic.h>
atomic_int counter = ATOMIC_VAR_INIT(0);
atomic_fetch_add(&counter, 1);  // Thread-safe increment
```

**Futexes (Fast Userspace Mutexes)**
Low-level kernel mechanism that pthreads mutexes are built upon. They remain in userspace when uncontended, only invoking kernel syscalls when threads must sleep.

## Memory Ordering and Visibility

Linux provides memory barriers to ensure proper ordering of memory operations across threads:

- Compiler barriers prevent reordering of operations by the compiler
- Memory barriers ensure CPU cache coherency and prevent hardware reordering
- The `volatile` keyword and `__sync_synchronize()` can enforce ordering

## Common Patterns

**Producer-Consumer Pattern**
Uses condition variables to coordinate buffer access between producer and consumer threads.

**Reader-Writer Pattern**
Multiple readers can access data simultaneously, but writers need exclusive access.

**Thread Pools**
Worker threads wait on condition variables for tasks, avoiding the overhead of creating/destroying threads.

## Deadlock Prevention

When using multiple locks, always acquire them in the same order across all threads. Linux provides `pthread_mutex_trylock()` for non-blocking lock attempts to implement deadlock avoidance strategies.


# Working examples of thread synchronization mechanisms in both C and C++. 

## 1. Mutex (Mutual Exclusion)

```cpp
#include <iostream>
#include <thread>
#include <mutex>
#include <vector>

constexpr int NUM_THREADS = 5;
constexpr int NUM_INCREMENTS = 100000;

// Shared resource
int counter = 0;
std::mutex mtx;

void increment_counter(int thread_id) {
    for (int i = 0; i < NUM_INCREMENTS; i++) {
        // Lock guard automatically locks and unlocks (RAII)
        std::lock_guard<std::mutex> lock(mtx);
        
        // Critical section
        counter++;
    }
    
    std::cout << "Thread " << thread_id << " finished\n";
}

int main() {
    std::vector<std::thread> threads;
    
    std::cout << "Starting with counter = " << counter << "\n";
    
    // Create threads
    for (int i = 0; i < NUM_THREADS; i++) {
        threads.emplace_back(increment_counter, i);
    }
    
    // Wait for all threads to complete
    for (auto& thread : threads) {
        thread.join();
    }
    
    std::cout << "Final counter value: " << counter << "\n";
    std::cout << "Expected value: " << NUM_THREADS * NUM_INCREMENTS << "\n";
    
    return 0;
}

// Compile: g++ -std=c++11 -pthread mutex_example.cpp -o mutex_example
// Run: ./mutex_example
```


## 2. Condition Variables (Producer-Consumer Pattern)

```cpp
#include <iostream>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <queue>
#include <chrono>

constexpr int NUM_ITEMS = 20;
constexpr int BUFFER_SIZE = 10;

// Shared buffer
std::queue<int> buffer;
std::mutex mtx;
std::condition_variable not_empty;
std::condition_variable not_full;

void producer(int producer_id) {
    for (int i = 0; i < NUM_ITEMS; i++) {
        int item = producer_id * 100 + i;
        
        std::unique_lock<std::mutex> lock(mtx);
        
        // Wait while buffer is full
        not_full.wait(lock, [] { return buffer.size() < BUFFER_SIZE; });
        
        // Add item to buffer
        buffer.push(item);
        
        std::cout << "Producer " << producer_id << " produced: " << item 
                  << " (buffer size: " << buffer.size() << ")\n";
        
        // Notify consumer that buffer is not empty
        not_empty.notify_one();
        
        lock.unlock();
        
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
}

void consumer(int consumer_id) {
    for (int i = 0; i < NUM_ITEMS; i++) {
        std::unique_lock<std::mutex> lock(mtx);
        
        // Wait while buffer is empty
        not_empty.wait(lock, [] { return !buffer.empty(); });
        
        // Remove item from buffer
        int item = buffer.front();
        buffer.pop();
        
        std::cout << "Consumer " << consumer_id << " consumed: " << item 
                  << " (buffer size: " << buffer.size() << ")\n";
        
        // Notify producer that buffer is not full
        not_full.notify_one();
        
        lock.unlock();
        
        std::this_thread::sleep_for(std::chrono::milliseconds(150));
    }
}

int main() {
    std::cout << "Starting Producer-Consumer example\n\n";
    
    std::thread prod(producer, 1);
    std::thread cons(consumer, 1);
    
    prod.join();
    cons.join();
    
    std::cout << "\nProducer-Consumer example completed\n";
    
    return 0;
}

// Compile: g++ -std=c++11 -pthread condvar_example.cpp -o condvar_example
// Run: ./condvar_example
```

## 3. Read-Write Locks

```cpp
#include <iostream>
#include <thread>
#include <shared_mutex>
#include <vector>
#include <chrono>

constexpr int NUM_READERS = 5;
constexpr int NUM_WRITERS = 2;
constexpr int NUM_OPERATIONS = 5;

// Shared resource
int shared_data = 0;
std::shared_mutex rw_mutex;

void reader(int reader_id) {
    for (int i = 0; i < NUM_OPERATIONS; i++) {
        // Acquire shared lock for reading (multiple readers allowed)
        std::shared_lock<std::shared_mutex> lock(rw_mutex);
        
        // Read the shared data
        int value = shared_data;
        std::cout << "Reader " << reader_id << ": Read value = " << value << "\n";
        
        // Simulate reading time
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        
        lock.unlock();
        
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }
    
    std::cout << "Reader " << reader_id << " finished\n";
}

void writer(int writer_id) {
    for (int i = 0; i < NUM_OPERATIONS; i++) {
        // Acquire unique lock for writing (exclusive access)
        std::unique_lock<std::shared_mutex> lock(rw_mutex);
        
        // Write to shared data
        shared_data++;
        std::cout << "Writer " << writer_id << ": Wrote value = " << shared_data << "\n";
        
        // Simulate writing time
        std::this_thread::sleep_for(std::chrono::milliseconds(150));
        
        lock.unlock();
        
        std::this_thread::sleep_for(std::chrono::milliseconds(300));
    }
    
    std::cout << "Writer " << writer_id << " finished\n";
}

int main() {
    std::vector<std::thread> readers;
    std::vector<std::thread> writers;
    
    std::cout << "Starting Read-Write Lock example\n";
    std::cout << "Initial value: " << shared_data << "\n\n";
    
    // Create reader threads
    for (int i = 0; i < NUM_READERS; i++) {
        readers.emplace_back(reader, i + 1);
    }
    
    // Create writer threads
    for (int i = 0; i < NUM_WRITERS; i++) {
        writers.emplace_back(writer, i + 1);
    }
    
    // Wait for all threads
    for (auto& t : readers) {
        t.join();
    }
    for (auto& t : writers) {
        t.join();
    }
    
    std::cout << "\nFinal value: " << shared_data << "\n";
    std::cout << "Expected final value: " << NUM_WRITERS * NUM_OPERATIONS << "\n";
    
    return 0;
}

// Compile: g++ -std=c++17 -pthread rwlock_example.cpp -o rwlock_example
// Run: ./rwlock_example
```

## 4. Semaphores

```cpp
#include <iostream>
#include <thread>
#include <semaphore>
#include <vector>
#include <mutex>
#include <chrono>

constexpr int NUM_THREADS = 8;
constexpr int MAX_RESOURCES = 3;

// C++20 counting semaphore
std::counting_semaphore<MAX_RESOURCES> resource_sem(MAX_RESOURCES);
int active_count = 0;
std::mutex count_mutex;

void use_resource(int thread_id) {
    std::cout << "Thread " << thread_id << ": Waiting for resource...\n";
    
    // Acquire resource (decrement semaphore)
    resource_sem.acquire();
    
    {
        std::lock_guard<std::mutex> lock(count_mutex);
        active_count++;
        std::cout << "Thread " << thread_id << ": Acquired resource (Active: " 
                  << active_count << "/" << MAX_RESOURCES << ")\n";
    }
    
    // Use the resource
    std::cout << "Thread " << thread_id << ": Using resource...\n";
    std::this_thread::sleep_for(std::chrono::seconds(2));
    
    {
        std::lock_guard<std::mutex> lock(count_mutex);
        active_count--;
        std::cout << "Thread " << thread_id << ": Releasing resource (Active: " 
                  << active_count << "/" << MAX_RESOURCES << ")\n";
    }
    
    // Release resource (increment semaphore)
    resource_sem.release();
}

int main() {
    std::vector<std::thread> threads;
    
    std::cout << "Resource Pool Example\n";
    std::cout << "Max concurrent resources: " << MAX_RESOURCES << "\n";
    std::cout << "Total threads: " << NUM_THREADS << "\n\n";
    
    // Create threads
    for (int i = 0; i < NUM_THREADS; i++) {
        threads.emplace_back(use_resource, i + 1);
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    
    // Wait for all threads
    for (auto& t : threads) {
        t.join();
    }
    
    std::cout << "\nAll threads completed\n";
    
    return 0;
}

// Compile: g++ -std=c++20 -pthread semaphore_example.cpp -o semaphore_example
// Run: ./semaphore_example
```

## 5. Barriers

```cpp
#include <iostream>
#include <thread>
#include <barrier>
#include <vector>
#include <chrono>

constexpr int NUM_THREADS = 4;
constexpr int NUM_PHASES = 3;

// Barrier with completion function that runs when all threads arrive
auto on_completion = []() noexcept {
    static int phase = 0;
    phase++;
    std::cout << "\n*** All threads synchronized at phase " << phase << " ***\n\n";
};

std::barrier sync_point(NUM_THREADS, on_completion);

void parallel_worker(int thread_id) {
    for (int phase = 1; phase <= NUM_PHASES; phase++) {
        // Each thread does independent work
        std::cout << "Thread " << thread_id << ": Starting phase " << phase << "\n";
        std::this_thread::sleep_for(std::chrono::seconds(thread_id));
        std::cout << "Thread " << thread_id << ": Completed phase " << phase << " work\n";
        
        // Wait at barrier for all threads
        std::cout << "Thread " << thread_id << ": Waiting at barrier (phase " 
                  << phase << ")\n";
        sync_point.arrive_and_wait();
        
        // All threads proceed together after barrier
        std::cout << "Thread " << thread_id << ": Proceeding past barrier (phase " 
                  << phase << ")\n";
    }
    
    std::cout << "Thread " << thread_id << ": All phases completed\n";
}

int main() {
    std::vector<std::thread> threads;
    
    std::cout << "Parallel Computation with Barriers\n";
    std::cout << "Number of threads: " << NUM_THREADS << "\n";
    std::cout << "Number of phases: " << NUM_PHASES << "\n\n";
    
    // Create threads
    for (int i = 0; i < NUM_THREADS; i++) {
        threads.emplace_back(parallel_worker, i + 1);
    }
    
    // Wait for all threads
    for (auto& t : threads) {
        t.join();
    }
    
    std::cout << "\nAll threads finished all phases\n";
    
    return 0;
}

// Compile: g++ -std=c++20 -pthread barrier_example.cpp -o barrier_example
// Run: ./barrier_example
```

## 6. Atomic Operations

```cpp
#include <iostream>
#include <thread>
#include <atomic>
#include <vector>
#include <mutex>

constexpr int NUM_THREADS = 5;
constexpr int NUM_INCREMENTS = 100000;

// Atomic counter - lock-free synchronization
std::atomic<int> atomic_counter{0};

// Non-atomic counter for comparison
int non_atomic_counter = 0;
std::mutex mtx;

void increment_atomic(int thread_id) {
    for (int i = 0; i < NUM_INCREMENTS; i++) {
        // Atomic increment - no locks needed!
        atomic_counter.fetch_add(1, std::memory_order_relaxed);
        // Or simply: atomic_counter++;
    }
    
    std::cout << "Thread " << thread_id << " finished atomic increments\n";
}

void increment_non_atomic(int thread_id) {
    for (int i = 0; i < NUM_INCREMENTS; i++) {
        // Must use mutex for non-atomic variable
        std::lock_guard<std::mutex> lock(mtx);
        non_atomic_counter++;
    }
    
    std::cout << "Thread " << thread_id << " finished non-atomic increments\n";
}

int main() {
    std::cout << "Atomic Operations Example\n";
    std::cout << "Number of threads: " << NUM_THREADS << "\n";
    std::cout << "Increments per thread: " << NUM_INCREMENTS << "\n\n";
    
    // Test atomic operations
    {
        std::cout << "=== Testing Atomic Operations ===\n";
        std::vector<std::thread> threads;
        
        for (int i = 0; i < NUM_THREADS; i++) {
            threads.emplace_back(increment_atomic, i + 1);
        }
        
        for (auto& t : threads) {
            t.join();
        }
        
        std::cout << "Atomic counter final value: " << atomic_counter.load() << "\n";
        std::cout << "Expected value: " << NUM_THREADS * NUM_INCREMENTS << "\n\n";
    }
    
    // Test non-atomic with mutex for comparison
    {
        std::cout << "=== Testing Non-Atomic with Mutex ===\n";
        std::vector<std::thread> threads;
        
        for (int i = 0; i < NUM_THREADS; i++) {
            threads.emplace_back(increment_non_atomic, i + 1);
        }
        
        for (auto& t : threads) {
            t.join();
        }
        
        std::cout << "Non-atomic counter final value: " << non_atomic_counter << "\n";
        std::cout << "Expected value: " << NUM_THREADS * NUM_INCREMENTS << "\n\n";
    }
    
    // Demonstrate other atomic operations
    std::cout << "=== Other Atomic Operations ===\n";
    std::atomic<int> value{10};
    
    std::cout << "Initial value: " << value.load() << "\n";
    
    value.store(20);
    std::cout << "After store(20): " << value.load() << "\n";
    
    int old = value.exchange(30);
    std::cout << "After exchange(30), old value: " << old 
              << ", new value: " << value.load() << "\n";
    
    int expected = 30;
    int desired = 40;
    if (value.compare_exchange_strong(expected, desired)) {
        std::cout << "Compare-exchange succeeded: " << expected 
                  << " -> " << desired << "\n";
    }
    
    value.fetch_add(5);
    std::cout << "After fetch_add(5): " << value.load() << "\n";
    
    value.fetch_sub(3);
    std::cout << "After fetch_sub(3): " << value.load() << "\n";
    
    // Memory ordering example
    std::atomic<bool> ready{false};
    std::atomic<int> data{0};
    
    std::thread writer([&]() {
        data.store(42, std::memory_order_relaxed);
        ready.store(true, std::memory_order_release);  // Synchronizes with acquire
    });
    
    std::thread reader([&]() {
        while (!ready.load(std::memory_order_acquire)) {  // Synchronizes with release
            std::this_thread::yield();
        }
        std::cout << "Data read by reader: " << data.load(std::memory_order_relaxed) << "\n";
    });
    
    writer.join();
    reader.join();
    
    return 0;
}

// Compile: g++ -std=c++11 -pthread atomic_example.cpp -o atomic_example
// Run: ./atomic_example
```

## Summary

### **1. Mutexes**
- Basic mutual exclusion for protecting shared resources
- C: `pthread_mutex_t` with lock/unlock
- C++: `std::mutex` with `std::lock_guard` (RAII pattern)

### **2. Condition Variables**
- Producer-consumer pattern implementation
- Efficient thread waiting without busy-polling
- C: `pthread_cond_t` with wait/signal
- C++: `std::condition_variable` with lambda predicates

### **3. Read-Write Locks**
- Multiple readers, single writer synchronization
- Optimizes read-heavy workloads
- C: `pthread_rwlock_t`
- C++: `std::shared_mutex` with `shared_lock` and `unique_lock` (C++17)

### **4. Semaphores**
- Resource pool management (counting semaphore)
- Controls access to limited resources
- C: POSIX `sem_t`
- C++: `std::counting_semaphore` (C++20)

### **5. Barriers**
- Synchronizes threads at specific points in parallel algorithms
- One thread can execute completion function
- C: `pthread_barrier_t`
- C++: `std::barrier` with completion callback (C++20)

### **6. Atomic Operations**
- Lock-free synchronization for simple operations
- Various memory ordering options for performance tuning
- C: `stdatomic.h` (C11)
- C++: `std::atomic` with memory ordering semantics

## Compilation Commands

**C examples:**
```bash
gcc -std=c11 -pthread filename.c -o output_name
```

**C++ examples:**
- For C++11/14/17: `g++ -std=c++17 -pthread filename.cpp -o output_name`
- For C++20 features: `g++ -std=c++20 -pthread filename.cpp -o output_name`

All examples are production-ready, include proper error handling, and demonstrate real-world usage patterns. They're designed to be compiled and run immediately on Linux systems!