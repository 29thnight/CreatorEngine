import std;
import Creator.Concurrency.JobGraph;
import Creator.Core.LayerCatalog;
import Creator.Core.Layers;

job_graph_diagnostics* echo_graph_type(job_graph_diagnostics* value);
std::uint64_t graph_clock_from_other_unit();

namespace
{
    unsigned checks{};
    void require(bool condition, std::string_view label)
    {
        ++checks;
        if (!condition)
        {
            throw std::runtime_error(std::string(label));
        }
    }
    void graph_contract()
    {
        const job_graph_node node{{0, 2}};
        require(node.dependencies == std::vector<std::uint32_t>{0, 2}, "dependency order");
        job_graph_node_timing timing;
        require(timing.registered_ns == 0 && timing.ready_ns == 0 && timing.publish_begin_ns == 0 &&
                    timing.publish_end_ns == 0 && timing.start_ns == 0 && timing.body_end_ns == 0 && timing.complete_ns == 0,
                "default timestamps");
        require(timing.publish_thread == 0 && timing.execute_thread == 0 && timing.publish_os_thread == 0 &&
                    timing.execute_os_thread == 0 && !timing.dispatcher_route && !timing.skipped,
                "default dispatch identity");
        job_graph_diagnostics diagnostics;
        require(diagnostics.nodes.empty() && diagnostics.submitted_ns == 0 && diagnostics.dispatcher_begin_ns == 0 &&
                    diagnostics.completed_ns == 0 && diagnostics.dispatcher_handoffs == 0 && diagnostics.native_task_count == 0 &&
                    !diagnostics.native_storage_reused,
                "default submission diagnostics");
        diagnostics.nodes.resize(2);
        diagnostics.nodes[1].complete_ns = 123;
        const auto copy = diagnostics;
        diagnostics.nodes[1].complete_ns = 456;
        require(copy.nodes[1].complete_ns == 123, "diagnostics value copy");
        require(echo_graph_type(&diagnostics) == &diagnostics, "cross translation unit type identity");
        const auto before = job_timestamp_ns();
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
        require(graph_clock_from_other_unit() > before, "shared steady nanosecond clock");
    }
    void catalog_contract()
    {
        using namespace ce::layers;
        LayerCatalog catalog;
        const auto initial = catalog.Snapshot();
        require(initial->revision == 1 && initial->next_id == 2 && initial->active_mask() == 1, "default catalog");
        require(initial->find(default_layer) && initial->find("Default"), "default lookups");
        const auto actor = catalog.Add("Actor");
        const auto wall = catalog.Add("Wall");
        require(actor && wall, "add layers");
        const auto saved = catalog.Snapshot();
        const auto actor_slot = saved->find(*actor)->slot;
        require(saved->find("Actor")->id == *actor && !initial->find(*actor), "immutable snapshots");
        const auto duplicate = catalog.Add("Actor");
        require(!duplicate && duplicate.error() == error::duplicate && catalog.Snapshot() == saved, "transactional duplicate");
        require(!catalog.Rename(default_layer, "Changed") && !catalog.Retire(default_layer), "reserved default layer");
        require(bool(catalog.Rename(*actor, "Hero")), "rename layer");
        require(catalog.Snapshot()->find("Hero")->id == *actor && catalog.Snapshot()->find(*actor)->slot == actor_slot,
                "stable identity and slot");
        require(saved->find("Actor") && !saved->find("Hero"), "retained name snapshot");
        require(bool(catalog.Retire(*wall)), "retire layer");
        require(!catalog.Snapshot()->find(*wall) && !(catalog.Snapshot()->active_mask() & saved->find(*wall)->slot.Mask()),
                "retired layer excluded from lookup and mask");
        const auto replacement = catalog.Add("Replacement");
        require(replacement && catalog.Snapshot()->find(*replacement)->slot != saved->find(*wall)->slot, "retired slots are not reused");
        require(bool(catalog.Replace(*saved)), "restore snapshot");
        require(catalog.Snapshot()->find("Actor") && !catalog.Snapshot()->find(*replacement), "restore content and retire later slots");
        const auto restored = catalog.Snapshot();
        bool owner_rejected{}, reader_ok{};
        std::thread other([&]
        {
            const auto result = catalog.Add("ForeignOwner");
            owner_rejected = !result && result.error() == error::wrong_owner;
            reader_ok = catalog.Snapshot()->find(*actor) != nullptr;
        });
        other.join();
        require(owner_rejected && reader_ok && catalog.Snapshot() == restored, "owner mutation and foreign immutable reader");
        require(!catalog.Add(std::string_view{"bad\0name", 8}) && !catalog.Add(std::string(1025, 'a')), "invalid names");
        auto invalid = *restored;
        invalid.next_id = 1;
        require(!LayerCatalog::Validate(invalid), "invalid imported identity range");
        std::vector<std::string> legacy{"Default"};
        for (unsigned i = 1; i < 32; ++i)
        {
            legacy.push_back("Layer" + std::to_string(i));
        }
        const auto imported = LayerCatalog::ImportLegacy(legacy);
        require(imported && imported->active_mask() == 0xffffffffu, "32 legacy slots preserve mask bits");
        LayerCatalog full;
        require(bool(full.Replace(*imported)), "load full catalog");
        const auto before_full = full.Snapshot();
        const auto overflow = full.Add("Overflow");
        require(!overflow && overflow.error() == error::capacity_exceeded && full.Snapshot() == before_full, "bounded transactional capacity");
        legacy[0] = "Other";
        require(!LayerCatalog::ImportLegacy(legacy), "legacy default validation");
        require(!restored->find(layer_id{999}) && !restored->find("missing"), "explicit missing lookup");
    }
}
int main()
{
    try
    {
        graph_contract();
        catalog_contract();
        std::cout << "GRAPH_CATALOG_MODULES_OK checks=" << checks << '\n';
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
