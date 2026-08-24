#include "platform/macos/MacSystemInfo.hpp"
#import <Foundation/Foundation.h>
#import <Metal/Metal.h>
#include <sys/sysctl.h>
#include <sys/types.h>

namespace hv::platform {
namespace {

std::string sysctlString(const char* name) {
    size_t size = 0;
    if (sysctlbyname(name, nullptr, &size, nullptr, 0) != 0 || size == 0) return {};
    std::string out(size, '\0');
    if (sysctlbyname(name, out.data(), &size, nullptr, 0) != 0) return {};
    while (!out.empty() && out.back() == '\0') out.pop_back();
    return out;
}

int sysctlInt(const char* name, int fallback) {
    int value = fallback;
    size_t size = sizeof(value);
    sysctlbyname(name, &value, &size, nullptr, 0);
    return value;
}

unsigned long long sysctlU64(const char* name) {
    unsigned long long value = 0;
    size_t size = sizeof(value);
    sysctlbyname(name, &value, &size, nullptr, 0);
    return value;
}

} // namespace

SystemInfo querySystemInfo() {
    SystemInfo info;
    info.cpuBrand = sysctlString("machdep.cpu.brand_string");
    // Apple Silicon has no "brand_string" the same way Intel does on some
    // OS builds; hw.optional.arm64 is the authoritative arm64 check.
    info.isAppleSilicon = sysctlInt("hw.optional.arm64", 0) == 1;
    if (info.cpuBrand.empty()) info.isAppleSilicon ? info.cpuBrand = "Apple Silicon" : info.cpuBrand = "Unknown";

    info.physicalCores = sysctlInt("hw.physicalcpu", 0);
    info.performanceCores = sysctlInt("hw.perflevel0.physicalcpu", info.physicalCores);
    info.efficiencyCores = sysctlInt("hw.perflevel1.physicalcpu", 0);
    info.physicalMemoryBytes = sysctlU64("hw.memsize");

    NSOperatingSystemVersion v = [[NSProcessInfo processInfo] operatingSystemVersion];
    info.macOSMajor = static_cast<int>(v.majorVersion);
    info.macOSMinor = static_cast<int>(v.minorVersion);
    info.macOSVersion = std::to_string(v.majorVersion) + "." + std::to_string(v.minorVersion) +
                        "." + std::to_string(v.patchVersion);

    id<MTLDevice> device = MTLCreateSystemDefaultDevice();
    if (device) {
        info.hasMetal = true;
        info.gpuName = [[device name] UTF8String];
    }
    return info;
}

} // namespace hv::platform
