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