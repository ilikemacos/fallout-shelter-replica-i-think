#pragma once
// Named CPU scopes, sampled every frame and shown in the debug panel.
#include "core/Types.hpp"
#include "core/Time.hpp"
#include <array>
#include <string>

namespace hv {

class Profiler {
public:
    static constexpr int kMaxScopes = 32;

    struct Scope {
        std::string name;
        f64 lastMs = 0.0;
        f64 avgMs = 0.0;
        u32 calls = 0;
    };

    /// Stable index for a scope name; call once and cache.
    int scopeId(const std::string& name);
    void beginFrame();
    void endFrame();
    void add(int id, f64 ms);
    const std::array<Scope, kMaxScopes>& scopes() const { return scopes_; }
    int scopeCount() const { return count_; }

private:
    std::array<Scope, kMaxScopes> scopes_{};
    int count_ = 0;
};

/// RAII timer feeding a Profiler scope.
class ProfileScope {
public:
    ProfileScope(Profiler& p, int id) : profiler_(p), id_(id), start_(nowSeconds()) {}
    ~ProfileScope() { profiler_.add(id_, (nowSeconds() - start_) * 1000.0); }

private:
    Profiler& profiler_;
    int       id_;
    f64       start_;
};

} // namespace hv

#define HV_PROFILE(profiler, name)                                    \
    static const int _hv_scope_##__LINE__ = (profiler).scopeId(name); \
    ::hv::ProfileScope _hv_prof_##__LINE__((profiler), _hv_scope_##__LINE__)
