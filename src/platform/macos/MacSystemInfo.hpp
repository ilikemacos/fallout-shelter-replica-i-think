#pragma once
// Real hardware/OS facts for the installer's pre-flight checks and the
// debug panel — every field is queried live, never assumed or hard-coded.
#include <string>

namespace hv::platform {

struct SystemInfo {
    bool isAppleSilicon = false;
    std::string cpuBrand;
    std::string macOSVersion;
    int macOSMajor = 0, macOSMinor = 0;
    int physicalCores = 0;
    int performanceCores = 0;
    int efficiencyCores = 0;
    unsigned long long physicalMemoryBytes = 0;
    std::string gpuName;
    bool hasMetal = false;
};

SystemInfo querySystemInfo();

} // namespace hv::platform
