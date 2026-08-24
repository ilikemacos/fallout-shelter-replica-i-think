// macOS locations: saves and logs live under Application Support as the
// platform expects, shaders come out of the .app bundle when there is one.
#include "core/Paths.hpp"
#import <Foundation/Foundation.h>
#include <cstdio>
#include <sys/stat.h>

namespace hv::paths {
namespace {

std::string nsToStd(NSString* s) { return s ? std::string([s UTF8String]) : std::string(); }

std::string containerDir() {
    NSArray<NSString*>* dirs = NSSearchPathForDirectoriesInDomains(
        NSApplicationSupportDirectory, NSUserDomainMask, YES);
    NSString* base = dirs.count > 0 ? dirs[0]
                                    : [NSHomeDirectory() stringByAppendingPathComponent:@"Library/Application Support"];
    return nsToStd([base stringByAppendingPathComponent:@"Haven"]);
}

} // namespace

std::string appSupportDir() {
    static const std::string dir = containerDir();
    return dir;
}
std::string savesDir()   { return appSupportDir() + "/Saves"; }
std::string logsDir()    { return appSupportDir() + "/Logs"; }
std::string configFile() { return appSupportDir() + "/settings.cfg"; }

std::string resourceDir() {
    NSBundle* bundle = [NSBundle mainBundle];
    NSString* res = [bundle resourcePath];
    // Running the raw binary out of a build directory has no Resources folder;
    // fall back to the working directory so `shaders/` still resolves.
    if (res && [[NSFileManager defaultManager] fileExistsAtPath:
                [res stringByAppendingPathComponent:@"shaders"]]) {
        return nsToStd(res);
    }
    return ".";
}

bool ensureDirectory(const std::string& path) {
    NSString* p = [NSString stringWithUTF8String:path.c_str()];
    NSError* err = nil;
    [[NSFileManager defaultManager] createDirectoryAtPath:p
                             withIntermediateDirectories:YES
                                              attributes:nil
                                                   error:&err];
    BOOL isDir = NO;
    return [[NSFileManager defaultManager] fileExistsAtPath:p isDirectory:&isDir] && isDir;
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
