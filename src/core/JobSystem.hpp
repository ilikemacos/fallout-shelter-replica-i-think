#pragma once
// Minimal work-stealing-free thread pool. The simulation uses it to fan out
// resident AI across performance cores; the renderer uses it for culling.
#include "core/Types.hpp"
#include <atomic>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <queue>
#include <thread>

namespace hv {

class JobSystem {
public:
    /// `workers == 0` picks hardware_concurrency-1, clamped to [1, 8].
    void start(unsigned workers = 0);
    void stop();
    ~JobSystem() { stop(); }

    void enqueue(std::function<void()> job);
    /// Splits [0,count) into `workers` contiguous chunks and blocks until done.
    void parallelFor(size_t count, size_t minChunk, const std::function<void(size_t, size_t)>& body);
    void waitIdle();

    unsigned workerCount() const { return static_cast<unsigned>(threads_.size()); }
    bool running() const { return running_.load(std::memory_order_relaxed); }

private:
    void workerLoop();

    std::vector<std::thread>          threads_;
    std::queue<std::function<void()>> queue_;
    std::mutex                        mutex_;
    std::condition_variable           cv_;
    std::condition_variable           idleCv_;
    std::atomic<bool>                 running_{false};
    std::atomic<int>                  inFlight_{0};
};

} // namespace hv
