// Non-Apple fallback so the headless test-suite builds in CI containers.
#include "core/Paths.hpp"
#include <cstdio>
#include <cstdlib>
#include <sys/stat.h>
#include <unistd.h>

namespace hv::paths {
namespace {
std::string home() {
    if (const char* h = std::getenv("HOME")) return h;
    return ".";
}
} // namespace

std::string appSupportDir() {
    if (const char* xdg = std::getenv("XDG_DATA_HOME")) return std::string(xdg) + "/Haven";
    return home() + "/.local/share/Haven";
}
std::string savesDir()    { return appSupportDir() + "/Saves"; }
std::string logsDir()     { return appSupportDir() + "/Logs"; }
std::string configFile()  { return appSupportDir() + "/settings.cfg"; }
std::string resourceDir() { return "."; }

bool ensureDirectory(const std::string& path) {
    if (path.empty()) return false;
    std::string acc;
    for (size_t i = 0; i <= path.size(); ++i) {
        if (i == path.size() || path[i] == '/') {
            if (!acc.empty() && acc != "/") ::mkdir(acc.c_str(), 0755);
        }
        if (i < path.size()) acc.push_back(path[i]);
    }
    struct stat st{};
    return ::stat(path.c_str(), &st) == 0 && S_ISDIR(st.st_mode);
}
bool fileExists(const std::string& path) {
    struct stat st{};
    return ::stat(path.c_str(), &st) == 0 && S_ISREG(st.st_mode);
}
bool removeFile(const std::string& path) { return std::remove(path.c_str()) == 0; }
bool renameFile(const std::string& a, const std::string& b) {
    return std::rename(a.c_str(), b.c_str()) == 0;
}
unsigned long long fileSize(const std::string& path) {
    struct stat st{};
    if (::stat(path.c_str(), &st) != 0) return 0;
    return static_cast<unsigned long long>(st.st_size);
}

} // namespace hv::paths
