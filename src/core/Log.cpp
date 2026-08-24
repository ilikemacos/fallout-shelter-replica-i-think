#include "core/Log.hpp"
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <deque>
#include <mutex>

namespace hv::log {
namespace {

std::mutex              g_mutex;
std::FILE*              g_file = nullptr;
Level                   g_level = Level::Info;
std::deque<std::string> g_ring;
constexpr size_t        kRingCapacity = 256;

const char* levelName(Level l) {
    switch (l) {
        case Level::Trace: return "TRACE";
        case Level::Debug: return "DEBUG";
        case Level::Info:  return "INFO ";
        case Level::Warn:  return "WARN ";
        case Level::Error: return "ERROR";
    }
    return "?????";
}

const char* baseName(const char* path) {
    const char* slash = std::strrchr(path, '/');
    return slash ? slash + 1 : path;
}

} // namespace

void init(const std::string& logFilePath) {
    std::lock_guard<std::mutex> lock(g_mutex);
    if (g_file) std::fclose(g_file);
    g_file = std::fopen(logFilePath.c_str(), "w");
}

void shutdown() {
    std::lock_guard<std::mutex> lock(g_mutex);
    if (g_file) { std::fclose(g_file); g_file = nullptr; }
}

void setLevel(Level l) { g_level = l; }

std::string format(const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    va_list copy;
    va_copy(copy, args);
    const int n = std::vsnprintf(nullptr, 0, fmt, copy);
    va_end(copy);
    std::string out;
    if (n > 0) {
        out.resize(static_cast<size_t>(n));
        std::vsnprintf(out.data(), static_cast<size_t>(n) + 1, fmt, args);
    }
    va_end(args);
    return out;
}

void write(Level l, const char* file, int line, const std::string& msg) {
    if (static_cast<int>(l) < static_cast<int>(g_level)) return;
    std::time_t t = std::time(nullptr);
    std::tm tmv{};
#if defined(_WIN32)
    localtime_s(&tmv, &t);
#else
    localtime_r(&t, &tmv);
#endif
    char stamp[32];
    std::strftime(stamp, sizeof stamp, "%H:%M:%S", &tmv);

    const std::string line_str =
        std::string(stamp) + " [" + levelName(l) + "] " + msg +
        "  (" + baseName(file) + ":" + std::to_string(line) + ")";

    std::lock_guard<std::mutex> lock(g_mutex);
    std::fputs(line_str.c_str(), l >= Level::Warn ? stderr : stdout);
    std::fputc('\n', l >= Level::Warn ? stderr : stdout);
    if (g_file) {
        std::fputs(line_str.c_str(), g_file);
        std::fputc('\n', g_file);
        std::fflush(g_file);
    }
    g_ring.push_back(std::string(levelName(l)) + " " + msg);
    while (g_ring.size() > kRingCapacity) g_ring.pop_front();
}

std::vector<std::string> recent(size_t maxLines) {
    std::lock_guard<std::mutex> lock(g_mutex);
    std::vector<std::string> out;
    const size_t start = g_ring.size() > maxLines ? g_ring.size() - maxLines : 0;
    for (size_t i = start; i < g_ring.size(); ++i) out.push_back(g_ring[i]);
    return out;
}

} // namespace hv::log
