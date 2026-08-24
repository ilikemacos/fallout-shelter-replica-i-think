#pragma once
#include "core/Types.hpp"
#include <chrono>

namespace hv {

/// Monotonic wall clock in seconds since process start.
inline f64 nowSeconds() {
    using clock = std::chrono::steady_clock;
    static const clock::time_point start = clock::now();
    return std::chrono::duration<f64>(clock::now() - start).count();
}

/// Rolling average used for the frame-time readouts in the debug panel.
template <int N>
class RollingAverage {
public:
    void push(f64 v) {
        sum_ -= samples_[cursor_];
        samples_[cursor_] = v;
        sum_ += v;
        cursor_ = (cursor_ + 1) % N;
        if (count_ < N) ++count_;
        peak_ = 0.0;
        for (int i = 0; i < count_; ++i) peak_ = peak_ > samples_[i] ? peak_ : samples_[i];
    }
    f64 average() const { return count_ ? sum_ / count_ : 0.0; }
    f64 peak() const { return peak_; }
    int count() const { return count_; }
    f64 sample(int i) const { return samples_[i % N]; }

private:
    f64 samples_[N]{};
    f64 sum_ = 0.0, peak_ = 0.0;
    int cursor_ = 0, count_ = 0;
};

/// Fixed-step accumulator so the simulation is deterministic regardless of FPS.
class StepClock {
public:
    explicit StepClock(f64 step = 1.0 / 60.0) : step_(step) {}
    /// Feeds real time in, returns how many fixed steps to run this frame.
    int advance(f64 dt) {
        // A long stall (window drag, disk hitch) must not spiral into a
        // hundred catch-up steps; clamp what we are willing to make up.
        accumulator_ += dt > 0.25 ? 0.25 : dt;
        int steps = 0;
        while (accumulator_ >= step_ && steps < kMaxSteps) {
            accumulator_ -= step_;
            ++steps;
        }
        if (steps == kMaxSteps) accumulator_ = 0.0;
        return steps;
    }
    f64 step() const { return step_; }
    f32 stepF() const { return static_cast<f32>(step_); }
    f32 alpha() const { return static_cast<f32>(accumulator_ / step_); }

private:
    static constexpr int kMaxSteps = 8;
    f64 step_;
    f64 accumulator_ = 0.0;
};

} // namespace hv
