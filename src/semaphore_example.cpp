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