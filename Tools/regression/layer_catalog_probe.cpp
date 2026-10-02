#include "../../Engine/Utility_Framework/LayerCatalog.h"
#include "../../Engine/Physics/PhysicsCollisionPolicy.h"
#include "../../Engine/SceneRuntime/SceneLayerIndex.h"
#include "../../Engine/SceneRuntime/ProjectLayerSettingsIO.h"
#include <iostream>
#include <thread>
#include <vector>

namespace
{
int checks = 0;

void check(bool value, const char* label)
{
    ++checks;
    if (!value)
    {
        std::cerr << label << '\n';
        std::exit(1);
    }
}

void exercise()
{
    using namespace ce::layers;
    check(layer_slot::Make(31)->Mask() == 0x80000000u, "31 is the final valid bit");
    check(!layer_slot::Make(32) && !layer_slot::Make(33), "32/33 cannot become shift counts");
    LayerCatalog catalog;
    const auto original = catalog.Snapshot();
    const auto actor = catalog.Add("Actor");
    const auto wall = catalog.Add("Wall");
    check(actor && wall, "add common layers");
    const auto before = catalog.Snapshot();
    const auto actor_slot = before->Find(*actor)->slot;
    check(before->Find("Actor")->id == *actor, "name resolves stable identity");
    check(!catalog.Add("Actor") && catalog.Snapshot() == before, "duplicate failure is transactional");
    check(!catalog.Add(std::string_view("bad\0name", 8)) && !catalog.Rename(*actor, std::string_view("\xc0\x80", 2)),
          "malformed names cannot enter the catalog");
    check(!catalog.Rename(default_layer, "Changed") && !catalog.Retire(default_layer), "Default is reserved");
    check(bool(catalog.Rename(*actor, "Hero")), "rename succeeds");
    check(catalog.Snapshot()->Find("Hero")->id == *actor && catalog.Snapshot()->Find(*actor)->slot == actor_slot,
          "rename changes neither ID nor mask");
    check(before->Find("Actor") && !before->Find("Hero"), "retained snapshot remains immutable");
    check(!catalog.Snapshot()->Find("missing") && !catalog.Snapshot()->Find(layer_id{999}),
          "missing layer never silently maps to Default");

    ce::physics::PhysicsCollisionPolicy policy;
    const auto initial_policy = policy.Snapshot();
    auto definitions = catalog.Snapshot();
    check(bool(policy.Set(*definitions, *actor, *wall, false)), "update collision policy");
    const auto changed = policy.Snapshot();
    const auto wall_slot = definitions->Find(*wall)->slot;
    check(!changed->Allows(actor_slot, wall_slot) && !changed->Allows(wall_slot, actor_slot), "policy is symmetric");
    check(initial_policy->Allows(actor_slot, wall_slot), "reader-held policy survives update");
    const auto filter = changed->Filter(*definitions, *actor);
    check(filter && filter->belongs_to == actor_slot.Mask() && filter->query_layers == actor_slot.Mask() &&
              !(filter->collides_with & wall_slot.Mask()),
          "filter compiles common ID to shared bit space");
    check(!changed->Filter(*definitions, layer_id{999}), "unknown filter identity rejected");
    auto invalid_policy = *changed;
    invalid_policy.matrix[actor_slot.Value() * 32 + wall_slot.Value()] = 1;
    check(!policy.Replace(invalid_policy) && policy.Snapshot() == changed, "asymmetric matrix cannot commit");
    invalid_policy = *changed;
    invalid_policy.matrix[0] = 2;
    check(!policy.Replace(invalid_policy), "nonboolean matrix rejected");
    check(bool(ce::physics::PhysicsCollisionPolicy::ImportLegacy(changed->matrix)), "matrix import preserves source");
    check(!ce::physics::PhysicsCollisionPolicy::ImportLegacy(std::span(changed->matrix).first(100)),
          "truncated legacy matrix rejected");

    SceneLayerIndex index(7), destination(8);
    const EntityHandle entity{7, 12, 3};
    check(bool(index.Assign(*definitions, entity, *actor)) && bool(index.Assign(*definitions, entity, *actor)),
          "membership registration is idempotent");
    check(index.Members(*definitions, *actor)->size() == 1, "one membership only");
    check(bool(index.Assign(*definitions, entity, *wall)) && index.Members(*definitions, *actor)->empty(),
          "membership moves between layers");
    check(!index.Assign(*definitions, entity, layer_id{999}) && index.Members(*definitions, *wall)->size() == 1,
          "failed layer assignment preserves prior membership");
    check(!destination.Assign(*definitions, entity, *wall), "foreign scene identity rejected");
    check(!index.Assign(*definitions, EntityHandle{7, 3, 0}, *wall), "invalid generation rejected");
    check(bool(index.Remove(entity)) && bool(index.Remove(entity)), "lifecycle removal is idempotent");
    check(bool(destination.Assign(*definitions, EntityHandle{8, 12, 4}, *wall)), "DDOL destination uses new handle");
    check(index.Members(*definitions, *wall)->empty() && destination.Members(*definitions, *wall)->size() == 1,
          "old scene retains no moved membership");

    bool owner_rejected = false;
    std::jthread foreign([&] {
        owner_rejected = !catalog.Add("Foreign") && !policy.Set(*definitions, *actor, *wall, true) &&
                         !index.Remove(entity) && !index.Members(*definitions, *actor);
        check(catalog.Snapshot()->Find(*actor) != nullptr, "immutable snapshot is readable off owner");
    });
    foreign.join();
    check(owner_rejected, "foreign mutation rejected");

    check(bool(catalog.Retire(*actor)), "retire layer");
    auto replacement = catalog.Add("Hero");
    check(replacement && *replacement != *actor && catalog.Snapshot()->Find(*replacement)->slot != actor_slot,
          "retired name may return but ID and slot never reuse");
    check(!policy.Snapshot()->Filter(*catalog.Snapshot(), *actor), "retired layer cannot create filters");
    check(bool(catalog.Replace(*original)), "authoring restore succeeds");
    const auto restored = catalog.Add("AfterRestore");
    check(restored && restored->value > replacement->value && catalog.Snapshot()->Find(*restored)->slot.Value() > 3,
          "restore retains ID high water and tombstone slots");
    auto invalid = *catalog.Snapshot();
    invalid.definitions[1]->id = layer_id{999};
    check(!catalog.Replace(invalid), "replacement cannot reassign a occupied slot");

    std::vector<std::string> names{"Default"};
    for (int i = 1; i < 32; ++i)
        names.push_back("Layer" + std::to_string(i));
    auto imported = LayerCatalog::ImportLegacy(names);
    check(imported && imported->ActiveMask() == 0xffffffffu, "32 legacy layers preserve every old bit");
    LayerCatalog full;
    check(bool(full.Replace(*imported)) && !full.Add("33rd"), "33rd layer cannot commit");
    names.push_back("33rd");
    check(!LayerCatalog::ImportLegacy(names), "oversized migration rejected");
    names.pop_back();
    names.back() = names[1];
    check(!LayerCatalog::ImportLegacy(names), "duplicate migration rejected");
    names[0] = "Unknown";
    check(!LayerCatalog::ImportLegacy(names), "missing reserved Default rejected");
}

void exercise_project(const std::filesystem::path& path)
{
    using namespace ce::layers;
    ProjectLayerSettings project;
    const auto before = project.Snapshot();
    layer_id actor;
    check(bool(project.Change([&](LayerCatalog& catalog, ce::physics::PhysicsCollisionPolicy& policy) -> result<void> {
              const auto added = catalog.Add("플레이어");
              if (!added)
                  return std::unexpected(added.error());
              actor = *added;
              return policy.Set(*catalog.Snapshot(), actor, default_layer, false);
          })),
          "project commits definitions and policy together");
    const auto authored = project.Snapshot();
    check(authored->catalog.revision == authored->revision && authored->policy.revision == authored->revision &&
              authored->catalog.Find(actor) && !(authored->policy.Filter(authored->catalog, actor)->collides_with & 1),
          "single revision contains the paired definition and policy");
    check(bool(before->catalog.Find(default_layer)) && !before->catalog.Find(actor), "project reader retains old pair");
    check(!project.Change([&](LayerCatalog& catalog, ce::physics::PhysicsCollisionPolicy& policy) -> result<void> {
        check(bool(catalog.Add("MustRollback")), "stage definition before failed policy");
        return policy.Set(*catalog.Snapshot(), layer_id{999}, default_layer, true);
    }) && project.Snapshot() == authored,
          "failed second edit cannot publish partial catalog");

    ProjectLayerSettings publishing;
    check(bool(publishing.Restore(*authored)), "publication probe baseline");
    const auto publicationBefore = publishing.Snapshot();
    bool attempted = false;
    check(!publishing.Change(
              [&](LayerCatalog& catalog, auto&) -> result<void> {
                  const auto changed = catalog.Rename(actor, "SavedName");
                  return changed;
              },
              [&](const auto& prepared) -> result<void> {
                  attempted = true;
                  check(prepared.catalog.Find(actor)->name == "SavedName" && publishing.Snapshot() == publicationBefore,
                        "writer receives prepared pair before memory publication");
                  check(!publishing.Restore(*before), "publication callback cannot reenter a project mutation");
                  return std::unexpected(error::io_failure);
              }) &&
              attempted && publishing.Snapshot() == publicationBefore,
          "save failure preserves entire published pair and epoch");
    check(bool(publishing.Change([](auto&, auto&) -> result<void> { return {}; })),
          "publication failure releases mutation guard");

    auto encoded = ProjectLayerSettingsCodec::Encode(*authored);
    check(bool(encoded), "encode typed binary project settings");
    auto decoded = ProjectLayerSettingsCodec::Decode(*encoded);
    check(decoded && decoded->catalog.Find(actor)->name == "플레이어" &&
              decoded->policy.matrix == authored->policy.matrix,
          "decode preserves UTF8 IDs slots and matrix");
    check(ProjectLayerSettingsCodec::Encode(*decoded).value() == *encoded, "codec produces canonical bytes");
    const auto invalid_with_checksum = [&](std::size_t offset, std::byte replacement) {
        auto bytes = *encoded;
        bytes[offset] = replacement;
        std::uint64_t checksum = 14695981039346656037ull;
        for (const auto byte : std::span(bytes).first(bytes.size() - 8))
            checksum = (checksum ^ std::to_integer<std::uint8_t>(byte)) * 1099511628211ull;
        for (std::size_t i = 0; i < 8; ++i)
            bytes[bytes.size() - 8 + i] = std::byte{static_cast<std::uint8_t>(checksum >> (8 * i))};

        return ProjectLayerSettingsCodec::Decode(bytes);
    };
    check(!invalid_with_checksum(4, std::byte{2}), "future schema rejected with valid checksum");
    check(!invalid_with_checksum(16, std::byte{33}), "oversized record count rejected with valid checksum");
    check(!invalid_with_checksum(28, std::byte{32}), "invalid slot rejected with valid checksum");
    check(!invalid_with_checksum(29, std::byte{2}), "invalid retired flag rejected with valid checksum");
    check(!invalid_with_checksum(31, std::byte{255}), "untrusted name length rejected with valid checksum");
    check(!invalid_with_checksum(encoded->size() - 8 - 1024, std::byte{2}),
          "nonboolean matrix rejected with valid checksum");
    for (std::size_t size = 0; size < encoded->size(); ++size)
        check(!ProjectLayerSettingsCodec::Decode(std::span(*encoded).first(size)), "every truncated file rejected");
    for (std::size_t offset = 0; offset < encoded->size(); ++offset)
    {
        auto corrupt = *encoded;
        corrupt[offset] ^= std::byte{1};
        check(!ProjectLayerSettingsCodec::Decode(corrupt), "every single-byte corruption rejected");
    }
    auto oversized = *encoded;
    oversized.resize(ProjectLayerSettingsCodec::max_bytes + 1);
    check(!ProjectLayerSettingsCodec::Decode(oversized), "file size bound enforced");
    auto bad_name = *authored;
    bad_name.catalog.definitions[1]->name.assign("bad\0name", 8);
    check(!ProjectLayerSettingsCodec::Encode(bad_name), "embedded NUL cannot enter project asset");
    bad_name.catalog.definitions[1]->name.assign("\xc0\x80", 2);
    check(!ProjectLayerSettingsCodec::Encode(bad_name), "overlong UTF8 cannot enter project asset");
    bad_name.catalog.definitions[1]->name.assign(1025, 'a');
    check(!ProjectLayerSettingsCodec::Encode(bad_name), "name length bound enforced");
    auto bad_policy = *authored;
    bad_policy.policy.matrix[1] ^= 1;
    check(!project.Restore(bad_policy) && project.Snapshot() == authored,
          "failed policy restore preserves published authoring pair");
    check(bool(project.Change([&](LayerCatalog& catalog, auto&) -> result<void> {
              const auto added = catalog.Add("CreatedDuringPlay");
              return added ? result<void>{} : std::unexpected(added.error());
          })),
          "Play-time layer added");
    check(bool(project.Restore(*authored)), "Stop restores catalog and policy together");
    const auto restored = project.Snapshot();
    check(restored->catalog.Find(actor) && !restored->catalog.Find("CreatedDuringPlay") &&
              restored->policy.matrix == authored->policy.matrix && restored->revision > authored->revision,
          "authoring restore keeps monotonic runtime revision");
    check(restored->catalog.definitions[2]->retired && restored->catalog.next_id > authored->catalog.next_id,
          "Play-created identity and slot cannot be reused after Stop");
    bool rejected = false;
    std::jthread foreign([&] { rejected = !project.Restore(*authored); });
    foreign.join();
    check(rejected, "foreign project restore rejected");

    check(bool(ProjectLayerSettingsIO::Write(*restored,
                                             [&](std::span<const std::byte> bytes) {
                                                 std::ofstream file(path, std::ios::binary | std::ios::trunc);
                                                 file.write(reinterpret_cast<const char*>(bytes.data()),
                                                            static_cast<std::streamsize>(bytes.size()));
                                                 file.close();
                                                 return bool(file);
                                             })),
          "publish project fixture through writer boundary");
    auto reloaded = ProjectLayerSettingsIO::Read(path);
    check(reloaded && reloaded->catalog.Find(actor) && reloaded->catalog.definitions[2]->retired &&
              reloaded->policy.matrix == restored->policy.matrix,
          "file reload preserves catalog and policy");
    ProjectLayerSettings player;
    check(bool(player.Restore(*reloaded)), "Player accepts typed settings without authoring parser");
    check(!ProjectLayerSettingsIO::Write(*restored, [](auto) { return false; }), "missing publication handler fails");
    check(bool(ProjectLayerSettingsIO::Read(path)), "failed writer does not damage prior fixture");
    check(!ProjectLayerSettingsIO::Read(path.string() + ".missing"), "missing runtime asset fails explicitly");
}
} // namespace

int main(int argc, char** argv)
{
    for (int repeat = 0; repeat < 128; ++repeat)
        exercise();

    check(argc == 2, "fixture path required");
    exercise_project(argv[1]);

    std::cout << "{\"result\":\"LAYER_CATALOG_OK\",\"checks\":" << checks << "}\n";
}
