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