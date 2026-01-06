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