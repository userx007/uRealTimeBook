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