#pragma once
// Multiple save slots on disk, autosave, manual save, crash-safe atomic
// writes, corruption detection (CRC32) and forward save migration.
#include "sim/World.hpp"
#include <optional>
#include <string>
#include <vector>

namespace hv::save {

struct SlotInfo {
    i32 slot = 0;
    bool exists = false;
    std::string shelterName;
    i32 day = 0;
    i32 population = 0;
    f64 playSeconds = 0.0;
    std::string timestamp;   ///< human readable, local time
    bool corrupted = false;
};

class SaveManager {
public:
    explicit SaveManager(std::string directory);

    static constexpr int kSlotCount = 6;
    static constexpr int kAutosaveSlot = -1;   ///< separate from the numbered slots

    std::vector<SlotInfo> listSlots() const;
    SlotInfo describeSlot(int slot) const;

    /// Writes to a temp file then renames over the slot — a crash mid-write
    /// cannot corrupt the previous save. Also rotates one backup copy.
    bool save(int slot, const hv::sim::World& world);
    bool load(int slot, hv::sim::World& world);
    bool deleteSlot(int slot);

    bool autosave(const hv::sim::World& world);
    bool loadAutosave(hv::sim::World& world);

    /// Restores the ".bak" copy over a corrupted or unwanted slot.
    bool restoreBackup(int slot);

private:
    std::string pathFor(int slot) const;
    std::string backupPathFor(int slot) const;
    bool writeAtomic(const std::string& path, const std::vector<u8>& bytes);
    std::optional<std::vector<u8>> readVerified(const std::string& path, bool* corrupted = nullptr) const;

    std::string dir_;
};

} // namespace hv::save
