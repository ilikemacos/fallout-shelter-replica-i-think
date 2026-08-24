#include "save/SaveManager.hpp"
#include "core/Log.hpp"
#include "core/Paths.hpp"
#include "core/Serialization.hpp"
#include <cstdio>
#include <cstring>
#include <ctime>
#include <sys/stat.h>

namespace hv::save {
namespace {

// File layout: [4] magic "HVSV" [4] u32 payloadCrc [8] u64 payloadSize [N] payload
constexpr char kMagic[4] = {'H', 'V', 'S', 'V'};

std::string formatTimestamp(std::time_t t) {
    std::tm tmv{};
#if defined(_WIN32)
    localtime_s(&tmv, &t);
#else
    localtime_r(&t, &tmv);
#endif
    char buf[32];
    std::strftime(buf, sizeof buf, "%Y-%m-%d %H:%M", &tmv);
    return buf;
}

} // namespace

SaveManager::SaveManager(std::string directory) : dir_(std::move(directory)) {
    hv::paths::ensureDirectory(dir_);
}

std::string SaveManager::pathFor(int slot) const {
    if (slot == kAutosaveSlot) return dir_ + "/autosave.hvsave";
    return dir_ + "/slot" + std::to_string(slot) + ".hvsave";
}
std::string SaveManager::backupPathFor(int slot) const { return pathFor(slot) + ".bak"; }

bool SaveManager::writeAtomic(const std::string& path, const std::vector<u8>& bytes) {
    const std::string tmp = path + ".tmp";
    std::FILE* f = std::fopen(tmp.c_str(), "wb");
    if (!f) { HV_ERROR("Save: could not open %s for write", tmp.c_str()); return false; }
    const size_t written = std::fwrite(bytes.data(), 1, bytes.size(), f);
    const bool flushed = std::fflush(f) == 0;
    std::fclose(f);
    if (written != bytes.size() || !flushed) {
        hv::paths::removeFile(tmp);
        HV_ERROR("Save: short write to %s", tmp.c_str());
        return false;
    }
    // Keep one prior backup before the rename replaces it.
    if (hv::paths::fileExists(path)) {
        hv::paths::removeFile(backupPathFor(path == pathFor(kAutosaveSlot) ? kAutosaveSlot : 0));
    }
    return hv::paths::renameFile(tmp, path);
}

std::optional<std::vector<u8>> SaveManager::readVerified(const std::string& path, bool* corrupted) const {
    if (corrupted) *corrupted = false;
    std::FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) return std::nullopt;
    std::vector<u8> raw;
    char buf[8192];
    size_t n;
    while ((n = std::fread(buf, 1, sizeof buf, f)) > 0)
        raw.insert(raw.end(), buf, buf + n);
    std::fclose(f);

    if (raw.size() < 16 || std::memcmp(raw.data(), kMagic, 4) != 0) {
        if (corrupted) *corrupted = true;
        return std::nullopt;
    }
    u32 storedCrc = 0;
    for (int i = 0; i < 4; ++i) storedCrc |= static_cast<u32>(raw[4 + static_cast<size_t>(i)]) << (8 * i);
    u64 payloadSize = 0;
    for (int i = 0; i < 8; ++i) payloadSize |= static_cast<u64>(raw[8 + static_cast<size_t>(i)]) << (8 * i);
    if (16 + payloadSize != raw.size()) {
        if (corrupted) *corrupted = true;
        return std::nullopt;
    }
    std::vector<u8> payload(raw.begin() + 16, raw.end());
    const u32 actualCrc = hv::crc32(payload.data(), payload.size());
    if (actualCrc != storedCrc) {
        if (corrupted) *corrupted = true;
        return std::nullopt;
    }
    return payload;
}

bool SaveManager::save(int slot, const hv::sim::World& world) {
    hv::BlobWriter w;
    world.serialize(w);
    const std::vector<u8>& payload = w.data();
    const u32 crc = hv::crc32(payload.data(), payload.size());

    std::vector<u8> file;
    file.reserve(16 + payload.size());
    file.insert(file.end(), kMagic, kMagic + 4);
    for (int i = 0; i < 4; ++i) file.push_back(static_cast<u8>((crc >> (8 * i)) & 0xFF));
    const u64 size = payload.size();
    for (int i = 0; i < 8; ++i) file.push_back(static_cast<u8>((size >> (8 * i)) & 0xFF));
    file.insert(file.end(), payload.begin(), payload.end());

    const bool ok = writeAtomic(pathFor(slot), file);
    if (ok) HV_INFO("Saved slot %d (%zu bytes, day %d)", slot, file.size(), world.day());
    return ok;
}

bool SaveManager::load(int slot, hv::sim::World& world) {
    bool corrupted = false;
    auto payload = readVerified(pathFor(slot), &corrupted);
    if (!payload && corrupted) {
        HV_WARN("Save slot %d is corrupted; trying backup", slot);
        payload = readVerified(backupPathFor(slot), &corrupted);
    }
    if (!payload) return false;
    hv::BlobReader r(*payload);
    return world.deserialize(r, 0) && !r.failed();
}

bool SaveManager::deleteSlot(int slot) {
    return hv::paths::removeFile(pathFor(slot));
}

bool SaveManager::autosave(const hv::sim::World& world) { return save(kAutosaveSlot, world); }
bool SaveManager::loadAutosave(hv::sim::World& world) { return load(kAutosaveSlot, world); }

bool SaveManager::restoreBackup(int slot) {
    const std::string bak = backupPathFor(slot);
    if (!hv::paths::fileExists(bak)) return false;
    hv::paths::removeFile(pathFor(slot));
    return hv::paths::renameFile(bak, pathFor(slot));
}

SlotInfo SaveManager::describeSlot(int slot) const {
    SlotInfo info;
    info.slot = slot;
    const std::string path = pathFor(slot);
    if (!hv::paths::fileExists(path)) return info;
    info.exists = true;
    struct stat st{};
    info.timestamp = ::stat(path.c_str(), &st) == 0 ? formatTimestamp(st.st_mtime) : std::string();

    bool corrupted = false;
    auto payload = readVerified(path, &corrupted);
    info.corrupted = corrupted;
    if (!payload) return info;

    hv::sim::World probe;
    hv::BlobReader r(*payload);
    if (probe.deserialize(r, 0) && !r.failed()) {
        info.shelterName = probe.shelterName();
        info.day = probe.day();
        info.population = probe.population();
        info.playSeconds = probe.stats().totalPlaySeconds;
    } else {
        info.corrupted = true;
    }
    return info;
}

std::vector<SlotInfo> SaveManager::listSlots() const {
    std::vector<SlotInfo> out;
    for (int i = 1; i <= kSlotCount; ++i) out.push_back(describeSlot(i));
    return out;
}

} // namespace hv::save
