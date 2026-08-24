#include "core/Profiler.hpp"

namespace hv {

int Profiler::scopeId(const std::string& name) {
    for (int i = 0; i < count_; ++i) if (scopes_[i].name == name) return i;
    if (count_ >= kMaxScopes) return kMaxScopes - 1;
    scopes_[count_].name = name;
    return count_++;
}

void Profiler::beginFrame() {
    for (int i = 0; i < count_; ++i) { scopes_[i].lastMs = 0.0; scopes_[i].calls = 0; }
}

void Profiler::endFrame() {
    // Exponential smoothing keeps the readout legible instead of jittering.
    for (int i = 0; i < count_; ++i)
        scopes_[i].avgMs = scopes_[i].avgMs * 0.9 + scopes_[i].lastMs * 0.1;
}

void Profiler::add(int id, f64 ms) {
    if (id < 0 || id >= count_) return;
    scopes_[id].lastMs += ms;
    ++scopes_[id].calls;
}

} // namespace hv
