// Headless simulation test-suite. No window, no GPU — exercises the ECS,
// shelter grid, resource economy, save round-trip, combat and AI systems
// that the rest of the game is built on.
#include "core/Math.hpp"
#include "core/Random.hpp"
#include "core/Serialization.hpp"
#include "core/Settings.hpp"
#include "core/JobSystem.hpp"
#include "ecs/Registry.hpp"
#include "sim/World.hpp"
#include "sim/Shelter.hpp"
#include "gameplay/Combat.hpp"
#include "save/SaveManager.hpp"
#include "scene/Primitives.hpp"
#include <cstdio>
#include <cstdlib>
#include <string>

namespace {

int g_failures = 0;
int g_checks = 0;

void expect(bool cond, const char* expr, const char* file, int line) {
    ++g_checks;
    if (!cond) {
        ++g_failures;
        std::fprintf(stderr, "FAIL: %s (%s:%d)\n", expr, file, line);
    }
}

} // namespace

#define CHECK(cond) expect((cond), #cond, __FILE__, __LINE__)
#define NEAR(a, b, eps) expect(std::fabs((a) - (b)) < (eps), #a " ~= " #b, __FILE__, __LINE__)

using namespace hv;
using namespace hv::sim;

// ---------------------------------------------------------------------------
static void test_math() {
    Mat4 t = Mat4::translate(Vec3{1, 2, 3});
    Vec3 p = t.transformPoint(Vec3{0, 0, 0});
    NEAR(p.x, 1.0f, 1e-5f); NEAR(p.y, 2.0f, 1e-5f); NEAR(p.z, 3.0f, 1e-5f);

    Mat4 inv = t.affineInverse();
    Vec3 back = inv.transformPoint(p);
    NEAR(back.x, 0.0f, 1e-4f); NEAR(back.y, 0.0f, 1e-4f); NEAR(back.z, 0.0f, 1e-4f);

    Mat4 proj = Mat4::perspective(60.0f * kDeg2Rad, 16.0f / 9.0f, 0.1f, 1000.0f);
    Mat4 view = Mat4::lookAt(Vec3{0, 5, 10}, Vec3{0, 0, 0}, Vec3{0, 1, 0});
    Frustum f = Frustum::fromViewProj(proj * view);
    CHECK(f.intersects(AABB{Vec3{-1, -1, -1}, Vec3{1, 1, 1}}));
    CHECK(!f.intersects(AABB{Vec3{5000, 5000, 5000}, Vec3{5001, 5001, 5001}}));

    AABB box{Vec3{-1, -1, -1}, Vec3{1, 1, 1}};
    Ray ray{Vec3{0, 0, -10}, Vec3{0, 0, 1}};
    f32 t0 = 0.0f;
    CHECK(ray.intersectAABB(box, t0));
    NEAR(t0, 9.0f, 1e-4f);
}

static void test_rng_determinism() {
    Rng a(1234), b(1234);
    for (int i = 0; i < 1000; ++i) CHECK(a.next() == b.next());

    Rng c(1), d(2);
    bool anyDiff = false;
    for (int i = 0; i < 50; ++i) if (c.next() != d.next()) anyDiff = true;
    CHECK(anyDiff);

    Rng e(99);
    int hi = 0;
    for (int i = 0; i < 10000; ++i) if (e.chance(0.3f)) ++hi;
    CHECK(hi > 2500 && hi < 3500);   // roughly 30% within a generous band
}

static void test_serialization_roundtrip() {
    BlobWriter w;
    w.u32v(0xDEADBEEF);
    w.f32v(3.14159f);
    w.str("hello shelter");
    w.boolv(true);
    w.vec3(Vec3{1, 2, 3});

    BlobReader r(w.data());
    CHECK(r.u32v() == 0xDEADBEEF);
    NEAR(r.f32v(), 3.14159f, 1e-5f);
    CHECK(r.str() == "hello shelter");
    CHECK(r.boolv() == true);
    Vec3 v = r.vec3();
    NEAR(v.x, 1.0f, 1e-5f);
    CHECK(!r.failed());

    // A reader given a truncated buffer must fail cleanly, never crash.
    std::vector<u8> truncated(w.data().begin(), w.data().begin() + 3);
    BlobReader broken(truncated);
    broken.u32v();
    CHECK(broken.failed());
}

static void test_crc32_detects_corruption() {
    std::vector<u8> data = {1, 2, 3, 4, 5, 6, 7, 8};
    const u32 c1 = crc32(data.data(), data.size());
    data[3] ^= 0xFF;
    const u32 c2 = crc32(data.data(), data.size());
    CHECK(c1 != c2);
}

static void test_ecs_basic() {
    ecs::Registry reg;
    struct Position { f32 x, y; };
    struct Health { f32 hp; };

    ecs::Entity e1 = reg.create();
    ecs::Entity e2 = reg.create();
    reg.add<Position>(e1, Position{1, 2});
    reg.add<Health>(e1, Health{100});
    reg.add<Position>(e2, Position{5, 6});

    CHECK(reg.get<Position>(e1)->x == 1);
    CHECK(reg.get<Health>(e2) == nullptr);

    int count = 0;
    reg.each<Position>([&](ecs::Entity, Position&) { ++count; });
    CHECK(count == 2);

    reg.destroy(e1);
    CHECK(!reg.alive(e1));
    CHECK(reg.get<Position>(e1) == nullptr);
    CHECK(reg.alive(e2));

    // A recycled index must not resurrect the old handle.
    ecs::Entity e3 = reg.create();
    CHECK(e3 != e1);
}

static void test_settings_roundtrip() {
    Settings s;
    s.graphics.applyPreset(QualityLevel::Ultra);
    s.graphics.windowWidth = 2560;
    s.audio.master = 0.42f;
    s.gameplay.autosaveMinutes = 7;

    const std::string text = s.serialize();
    Settings loaded;
    loaded.deserialize(text);
    CHECK(loaded.graphics.windowWidth == 2560);
    NEAR(loaded.audio.master, 0.42f, 1e-4f);
    CHECK(loaded.gameplay.autosaveMinutes == 7);
    CHECK(loaded.graphics.rayTracing == RayTracingMode::Low);
}

static void test_shelter_build_rules() {
    Shelter s;
    s.createStarter();
    ResourcePool res;
    res.setCapacity(Resource::Materials, 5000);
    res.setCapacity(Resource::Scrip, 5000);
    res.add(Resource::Materials, 2000);
    res.add(Resource::Scrip, 2000);

    // Cannot build floating in the middle of nowhere.
    BuildPlan bad = s.planBuild(RoomType::Generator, Cell{0, 12}, 3, res, 0, {});
    CHECK(!bad.ok());
    CHECK(bad.error == BuildError::NotAdjacent);

    // Adjacent to the starter airlock/elevator should work.
    BuildPlan good = s.planBuild(RoomType::Generator, Cell{0, 4}, 3, res, 0, {});
    CHECK(good.ok());
    RoomId genId = s.build(good, res);
    CHECK(genId != kNoRoom);
    CHECK(s.roomAt(Cell{0, 4})->type == RoomType::Generator);

    // Floor 1 needs an elevator before anything else can go there.
    BuildPlan noElevator = s.planBuild(RoomType::Storage, Cell{1, 5}, 2, res, 0, {});
    CHECK(!noElevator.ok());
    CHECK(noElevator.error == BuildError::NeedsElevator);

    BuildPlan elevator = s.planBuild(RoomType::Elevator, Cell{0, 3}, 1, res, 0, {});
    CHECK(elevator.ok());
    s.build(elevator, res);
    BuildPlan elevator2 = s.planBuild(RoomType::Elevator, Cell{1, 3}, 1, res, 0, {});
    CHECK(elevator2.ok());
    s.build(elevator2, res);

    BuildPlan nowOk = s.planBuild(RoomType::Storage, Cell{1, 4}, 2, res, 0, {});
    CHECK(nowOk.ok());
    s.build(nowOk, res);

    // Population gating.
    BuildPlan gated = s.planBuild(RoomType::Laboratory, Cell{1, 6}, 3, res, 2, {});
    CHECK(!gated.ok());
    CHECK(gated.error == BuildError::PopulationLocked);

    // Tech gating.
    BuildPlan techGated = s.planBuild(RoomType::Laboratory, Cell{1, 6}, 3, res, 20, {});
    CHECK(!techGated.ok());
    CHECK(techGated.error == BuildError::TechLocked);
    BuildPlan techOk = s.planBuild(RoomType::Laboratory, Cell{1, 6}, 3, res, 20,
                                   {"applied_chemistry"});
    CHECK(techOk.ok());

    // Same-type rooms placed side by side should merge into one wider room.
    const i32 roomsBefore = static_cast<i32>(s.roomCount());
    BuildPlan mergeBuild = s.planBuild(RoomType::Storage, Cell{1, 6}, 2, res, 0, {});
    CHECK(mergeBuild.ok());
    CHECK(mergeBuild.mergeInto != kNoRoom);
    s.build(mergeBuild, res);
    CHECK(static_cast<i32>(s.roomCount()) == roomsBefore);   // merged, not appended
    CHECK(s.room(mergeBuild.mergeInto)->width == 4);
}

static void test_resource_pool() {
    ResourcePool p;
    p.setCapacity(Resource::Water, 100);
    CHECK(p.add(Resource::Water, 150) == 100.0f);   // clipped to capacity
    NEAR(p.get(Resource::Water), 100.0f, 1e-4f);
    CHECK(p.spend(Resource::Water, 40));
    NEAR(p.get(Resource::Water), 60.0f, 1e-4f);
    CHECK(!p.spend(Resource::Water, 1000));
    NEAR(p.drain(Resource::Water, 1000), 940.0f, 1e-4f);   // shortfall reported
    NEAR(p.get(Resource::Water), 0.0f, 1e-4f);
}

static void test_resident_generation_and_progression() {
    Rng rng(42);
    Resident r = makeResident(rng, 1, 0.0f, 0);
    CHECK(!r.name.empty());
    CHECK(r.health > 0.0f);
    CHECK(r.effectiveSkill(Skill::Engineering) >= 1);

    const i32 lvl0 = r.level;
    r.grantExperience(10000.0f);
    CHECK(r.level > lvl0);
    CHECK(r.maxHealth > 90.0f);

    Rng rng2(7);
    Resident mom = makeResident(rng2, 2, 0.0f, 1);
    Resident dad = makeResident(rng2, 3, 0.0f, 1);
    Resident child = makeChild(rng2, 4, mom, dad);
    CHECK(child.age == 18.0f);
    CHECK(!child.name.empty());
}

static void test_combat_resolution() {
    Rng rng(5);
    gameplay::Encounter enc;
    Resident defender = makeResident(rng, 1, 0.0f, 2);
    defender.skills.set(Skill::Security, 9);
    defender.weapon = ItemStack{itemIdByName("Riot Shotgun"), 1, 1.0f};
    enc.addResident(defender, 0.4f, 0.1f);
    enc.addEnemy(gameplay::EnemyKind::Scavenger, 1, 0.6f);

    int iterations = 0;
    while (enc.tick(0.1f, rng) && iterations < 20000) ++iterations;
    CHECK(enc.state() != gameplay::CombatState::Active);
    CHECK(iterations < 20000);   // must actually converge
}

static void test_world_tick_produces_resources() {
    World w;
    w.newGame(123);
    const f32 materialsBefore = w.resources().get(Resource::Materials);

    BuildPlan plan = w.tryBuild(RoomType::Generator, Cell{0, 4}, 3);
    CHECK(plan.ok());
    CHECK(w.resources().get(Resource::Materials) < materialsBefore);

    // Force the room to be finished and staffed so production can run.
    Room* gen = nullptr;
    for (Room& r : w.shelter().rooms()) if (r.type == RoomType::Generator) gen = &r;
    CHECK(gen != nullptr);
    gen->buildProgress = 1.0f;

    i32 assigned = w.autoAssignAll();
    CHECK(assigned > 0);

    const f32 powerBefore = w.resources().get(Resource::Power);
    for (int i = 0; i < 600; ++i) w.tick(1.0f);   // ten minutes of sim time
    CHECK(w.resources().get(Resource::Power) != powerBefore);
    CHECK(w.day() >= 1);
    CHECK(w.population() > 0);
}

static void test_save_load_roundtrip(const std::string& dir) {
    World original;
    original.newGame(999);
    original.tryBuild(RoomType::Generator, Cell{0, 4}, 3);
    for (int i = 0; i < 120; ++i) original.tick(1.0f);
    const i32 popBefore = original.population();
    const f32 waterBefore = original.resources().get(Resource::Water);
    const std::string nameBefore = original.shelterName();

    hv::save::SaveManager mgr(dir);
    CHECK(mgr.save(1, original));

    World loaded;
    CHECK(mgr.load(1, loaded));
    CHECK(loaded.population() == popBefore);
    NEAR(loaded.resources().get(Resource::Water), waterBefore, 1e-2f);
    CHECK(loaded.shelterName() == nameBefore);
    CHECK(loaded.shelter().roomCount() == original.shelter().roomCount());

    // Corrupt the file and make sure loading fails safely rather than crashing.
    const std::string path = dir + "/slot1.hvsave";
    std::FILE* f = std::fopen(path.c_str(), "r+b");
    CHECK(f != nullptr);
    if (f) {
        std::fseek(f, 20, SEEK_SET);
        const unsigned char garbage = 0xFF;
        std::fwrite(&garbage, 1, 1, f);
        std::fclose(f);
    }
    World corruptLoad;
    // Corruption is detected (CRC mismatch); load() must not crash, and may
    // legitimately fail since there is no backup yet for a fresh slot.
    (void)mgr.load(1, corruptLoad);
}

static void test_save_backup_rotation(const std::string& dir) {
    // Regression test: writeAtomic() used to compute the backup path from
    // slot 0 (not a real slot) for every numbered slot, so a save's ".bak"
    // sibling was never actually written and restoreBackup()/corruption
    // recovery silently did nothing for slots 1..6.
    hv::save::SaveManager mgr(dir + "_backup");

    World first;
    first.newGame(1);
    first.setShelterName("FirstSave");
    CHECK(mgr.save(2, first));

    World second;
    second.newGame(2);
    second.setShelterName("SecondSave");
    CHECK(mgr.save(2, second));   // must rotate FirstSave's bytes into slot2.hvsave.bak

    World loadedCurrent;
    CHECK(mgr.load(2, loadedCurrent));
    CHECK(loadedCurrent.shelterName() == "SecondSave");

    // Corrupt the live slot; load() must fall back to the rotated backup.
    const std::string path = dir + "_backup/slot2.hvsave";
    std::FILE* f = std::fopen(path.c_str(), "r+b");
    CHECK(f != nullptr);
    if (f) {
        std::fseek(f, 20, SEEK_SET);
        const unsigned char garbage = 0xFF;
        std::fwrite(&garbage, 1, 1, f);
        std::fclose(f);
    }
    World recovered;
    CHECK(mgr.load(2, recovered));
    CHECK(recovered.shelterName() == "FirstSave");
}

static void test_job_system_parallel_for() {
    JobSystem js;
    js.start(4);
    std::vector<int> data(10000, 0);
    js.parallelFor(data.size(), 64, [&](size_t begin, size_t end) {
        for (size_t i = begin; i < end; ++i) data[i] = static_cast<int>(i) * 2;
    });
    bool ok = true;
    for (size_t i = 0; i < data.size(); ++i) if (data[i] != static_cast<int>(i) * 2) ok = false;
    CHECK(ok);
    js.stop();
}

static void test_expedition_lifecycle() {
    World w;
    w.newGame(555);
    const gameplay::Site* site = nullptr;
    for (const gameplay::Site& s : w.surface().sites()) if (s.discovered) { site = &s; break; }
    CHECK(site != nullptr);
    if (!site) return;

    std::vector<ResidentId> squad;
    for (const Resident& r : w.residents()) { squad.push_back(r.id); if (squad.size() == 2) break; }
    const u32 expId = w.launchExpedition(site->id, squad);
    CHECK(expId != 0);
    CHECK(w.resident(squad[0])->expeditionId == expId);

    int guard = 0;
    while (w.expeditions().find(expId) != nullptr && guard < 6000) {
        w.tick(1.0f);
        ++guard;
    }
    CHECK(guard < 6000);   // the expedition must actually conclude
    CHECK(w.resident(squad[0])->expeditionId == 0);
}

static void test_quest_progression() {
    World w;
    w.newGame(31337);
    gameplay::Quest* q = nullptr;
    for (gameplay::Quest& x : w.quests().quests()) if (x.title == "Cold Start") q = &x;
    CHECK(q != nullptr);

    w.tryBuild(RoomType::Generator, Cell{0, 4}, 3);
    w.tryBuild(RoomType::WaterPlant, Cell{0, 1}, 3);
    w.tryBuild(RoomType::Hydroponics, Cell{0, 10}, 3);
    for (int i = 0; i < 5; ++i) w.tick(1.0f);

    q = nullptr;
    for (gameplay::Quest& x : w.quests().quests()) if (x.title == "Cold Start") q = &x;
    CHECK(q != nullptr);
    if (q) CHECK(q->state == gameplay::QuestState::Complete || q->state == gameplay::QuestState::Claimed);
}

static void test_emergency_kill_count_not_duplicated() {
    // Regression test: Encounter::residentKills() is a cumulative total for
    // the whole fight, but the emergency handler used to re-award every
    // already-counted kill on every tick the fight was still going, wildly
    // inflating stats/quest progress/XP the longer a fight took to resolve.
    World w;
    w.newGame(2024);
    CHECK(!w.residents().empty());
    if (w.residents().empty()) return;
    Resident& defender = w.residents().front();
    defender.skills.set(Skill::Security, 10);
    defender.weapon = ItemStack{itemIdByName("Breaker Cannon"), 1, 1.0f};
    defender.outfit = ItemStack{itemIdByName("Warden Harness"), 1, 1.0f};

    const std::vector<RoomId> entrances = w.shelter().roomsOfType(RoomType::Entrance);
    CHECK(!entrances.empty());
    if (entrances.empty()) return;
    const u32 emergencyId = w.events().trigger(EventKind::Intrusion, w, entrances.front(), 1.0f);
    CHECK(emergencyId != 0);
    Emergency* e = w.events().find(emergencyId);
    CHECK(e != nullptr);
    if (e) e->responders.push_back(defender.id);

    // Small steps so the fight spans many ticks rather than resolving in one.
    for (int i = 0; i < 600 && w.events().activeCount() > 0; ++i) w.tick(0.1f);

    // A single Intrusion spawns at most ~7 enemies (2 + severity<=3 + rng(0,2));
    // a correct implementation never reports more kills than could exist.
    CHECK(w.stats().enemiesDefeated <= 10);
}

static void test_primitive_winding_matches_normals() {
    // Regression test: appendQuadXZ wound its triangles so the geometric
    // (winding) normal pointed the opposite way from the vertex normal it
    // assigned, which meant every floor slab in the game was back-face
    // culled and simply never drawn — a large part of why the first build
    // rendered an almost empty screen.
    //
    // Checks the whole class of bug: for every primitive, every triangle's
    // winding normal must agree with its vertices' shading normals.
    using namespace hv::scene;

    auto checkAgreement = [](const MeshBuild& m, const char* what) {
        CHECK(!m.i.empty());
        int disagreements = 0;
        for (size_t t = 0; t + 2 < m.i.size(); t += 3) {
            const hv::gfx::Vertex& v0 = m.v[m.i[t]];
            const hv::gfx::Vertex& v1 = m.v[m.i[t + 1]];
            const hv::gfx::Vertex& v2 = m.v[m.i[t + 2]];
            const Vec3 geo = cross(v1.position - v0.position, v2.position - v0.position);
            if (lengthSq(geo) < 1e-12f) continue;         // degenerate, e.g. cap centres
            const Vec3 shading = v0.normal + v1.normal + v2.normal;
            if (lengthSq(shading) < 1e-12f) continue;
            if (dot(normalize(geo), normalize(shading)) < 0.0f) ++disagreements;
        }
        if (disagreements != 0)
            std::fprintf(stderr, "  %s: %d triangles wound against their normals\n",
                         what, disagreements);
        expect(disagreements == 0, what, __FILE__, __LINE__);
    };

    { MeshBuild m; appendQuadXZ(m, {0,0,0}, 10.0f, 6.0f); checkAgreement(m, "appendQuadXZ"); }
    { MeshBuild m; appendQuadXY(m, {0,0,0}, 4.0f, 3.0f); checkAgreement(m, "appendQuadXY"); }
    { MeshBuild m; appendBox(m, {0,0,0}, {1.0f, 2.0f, 3.0f}); checkAgreement(m, "appendBox"); }
    { MeshBuild m; appendCylinder(m, {0,0,0}, 1.0f, 2.0f, 16); checkAgreement(m, "appendCylinder"); }
    { MeshBuild m; appendCapsule(m, {0,0,0}, 0.4f, 2.0f, 12); checkAgreement(m, "appendCapsule"); }
    { MeshBuild m; appendPipe(m, {0,0,0}, {2.0f,0,0}, 0.2f, 10); checkAgreement(m, "appendPipe"); }

    // A floor slab must specifically face upward, since that is what the
    // camera looks down at.
    MeshBuild floorMesh;
    appendQuadXZ(floorMesh, {0, 0, 0}, 10.0f, 6.0f);
    const hv::gfx::Vertex& a = floorMesh.v[floorMesh.i[0]];
    const hv::gfx::Vertex& b = floorMesh.v[floorMesh.i[1]];
    const hv::gfx::Vertex& c = floorMesh.v[floorMesh.i[2]];
    const Vec3 winding = normalize(cross(b.position - a.position, c.position - a.position));
    CHECK(winding.y > 0.9f);
}

int main() {
    test_math();
    test_rng_determinism();
    test_serialization_roundtrip();
    test_crc32_detects_corruption();
    test_ecs_basic();
    test_settings_roundtrip();
    test_shelter_build_rules();
    test_resource_pool();
    test_resident_generation_and_progression();
    test_combat_resolution();
    test_world_tick_produces_resources();
    test_job_system_parallel_for();
    test_expedition_lifecycle();
    test_quest_progression();
    test_emergency_kill_count_not_duplicated();
    test_primitive_winding_matches_normals();

    const char* tmp = std::getenv("TMPDIR");
    test_save_load_roundtrip(std::string(tmp ? tmp : "/tmp") + "/haven_test_saves");
    test_save_backup_rotation(std::string(tmp ? tmp : "/tmp") + "/haven_test_saves");

    std::printf("%d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
