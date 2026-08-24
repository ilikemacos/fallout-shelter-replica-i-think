#pragma once
#include <string>

namespace hv::paths {

/// ~/Library/Application Support/Haven on macOS (XDG data dir elsewhere).
std::string appSupportDir();
/// Directory holding save slots; created on demand.
std::string savesDir();
/// Directory for the log file and crash reports.
std::string logsDir();
/// settings.cfg lives here.
std::string configFile();
/// Resources inside the .app bundle (or the source tree when running loose).
std::string resourceDir();

bool ensureDirectory(const std::string& path);
bool fileExists(const std::string& path);
bool removeFile(const std::string& path);
bool renameFile(const std::string& from, const std::string& to);
/// Byte size, or 0 when missing.
unsigned long long fileSize(const std::string& path);

} // namespace hv::paths
