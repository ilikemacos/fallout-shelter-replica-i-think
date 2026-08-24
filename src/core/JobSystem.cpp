#include "core/JobSystem.hpp"
#include "core/Log.hpp"
#include <algorithm>

namespace hv {

void JobSystem::start(unsigned workers) {
    if (running_.load()) return;
    if (workers == 0) {
        const unsigned hc = std::thread::hardware_concurrency();
        workers = hc > 1 ? hc - 1 : 1;
        workers = std::min(workers, 8u);
    }
    running_.store(true);
    threads_.reserve(workers);
    for (unsigned i = 0; i < workers; ++i) threads_.emplace_back([this] { workerLoop(); });
    HV_INFO("Job system started with %u worker threads", workers);
}

void JobSystem::stop() {
    if (!running_.exchange(false)) return;
    cv_.notify_all();
    for (std::thread& t : threads_) if (t.joinable()) t.join();
    threads_.clear();
}

void JobSystem::workerLoop() {
    for (;;) {
        std::function<void()> job;
        {
            std::unique_lock<std::mutex> lock(mutex_);
            cv_.wait(lock, [this] { return !queue_.empty() || !running_.load(); });
            if (!running_.load() && queue_.empty()) return;
            job = std::move(queue_.front());
            queue_.pop();
        }
        job();
        if (inFlight_.fetch_sub(1) == 1) {
            std::lock_guard<std::mutex> lock(mutex_);
            idleCv_.notify_all();
        }
    }
}

void JobSystem::enqueue(std::function<void()> job) {
    if (!running_.load()) { job(); return; }
    inFlight_.fetch_add(1);
    {
        std::lock_guard<std::mutex> lock(mutex_);
        queue_.push(std::move(job));
    }
    cv_.notify_one();
}

void JobSystem::waitIdle() {
    std::unique_lock<std::mutex> lock(mutex_);
    idleCv_.wait(lock, [this] { return inFlight_.load() == 0; });
}

void JobSystem::parallelFor(size_t count, size_t minChunk,
                            const std::function<void(size_t, size_t)>& body) {
    if (count == 0) return;
    const size_t workers = running_.load() ? threads_.size() + 1 : 1;
    size_t chunk = (count + workers - 1) / workers;
    chunk = std::max(chunk, std::max<size_t>(minChunk, 1));
    if (chunk >= count || workers == 1) { body(0, count); return; }

    // The calling thread takes the first chunk so it is never idle.
    size_t cursor = chunk;
    std::vector<std::pair<size_t, size_t>> ranges;
    while (cursor < count) {
        const size_t end = std::min(cursor + chunk, count);
        ranges.emplace_back(cursor, end);
        cursor = end;
    }
    for (const auto& r : ranges) enqueue([&body, r] { body(r.first, r.second); });
    body(0, chunk);
    waitIdle();
}

} // namespace hv
