#pragma once
#include "core/Types.hpp"
#include <string>
#include <string_view>

namespace hv::log {

enum class Level { Trace, Debug, Info, Warn, Error };

void init(const std::string& logFilePath);
void shutdown();
void setLevel(Level l);
void write(Level l, const char* file, int line, const std::string& msg);
/// Last N lines, newest last — surfaced by the in-game debug console.
std::vector<std::string> recent(size_t maxLines = 64);

std::string format(const char* fmt, ...)
#if defined(__GNUC__)
    __attribute__((format(printf, 1, 2)))
#endif
    ;

} // namespace hv::log

#define HV_LOG(lvl, ...) ::hv::log::write(lvl, __FILE__, __LINE__, ::hv::log::format(__VA_ARGS__))
#define HV_TRACE(...) HV_LOG(::hv::log::Level::Trace, __VA_ARGS__)
#define HV_DEBUG(...) HV_LOG(::hv::log::Level::Debug, __VA_ARGS__)
#define HV_INFO(...)  HV_LOG(::hv::log::Level::Info,  __VA_ARGS__)
#define HV_WARN(...)  HV_LOG(::hv::log::Level::Warn,  __VA_ARGS__)
#define HV_ERROR(...) HV_LOG(::hv::log::Level::Error, __VA_ARGS__)
