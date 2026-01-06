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