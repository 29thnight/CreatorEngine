#include <new>
namespace material_graph
{
    inline bool failPacketGroupRebuild{};
    inline void CheckPacketGroupRebuild()
    {
        if (failPacketGroupRebuild)
        {
            failPacketGroupRebuild = false;
            throw std::bad_alloc();
        }
    }
} // namespace material_graph
#define CREATOR_PACKET_GROUP_REBUILD_TEST_HOOK material_graph::CheckPacketGroupRebuild()
#include "../../Engine/RenderEngine/MaterialGraphPacketGroups.h"
#include <array>
#include <iostream>
#include <map>
#include <stdexcept>
#include <crtdbg.h>
#include <exception>

namespace
{
    size_t checks{};
    void Require(bool condition)
    {
        ++checks;
        if (!condition)
        {
            throw std::runtime_error("Scene packet group regression failed");
        }
    }
    void Check(const material_graph::ScenePacketGroups& plan,
               std::span<const material_graph::ScenePacketGroups::Input> inputs)
    {
        std::map<std::tuple<std::string, bool, uint32_t>, std::vector<size_t>> expected;
        for (const auto& input : inputs)
        {
            expected[{std::string(input.semantic), input.doubleSided, input.page}].push_back(input.index);
        }
        Require(plan.Groups().size() == expected.size());
        size_t bucket{}, output{};
        for (const auto& [key, indices] : expected)
        {
            const auto& actual = plan.Groups()[bucket];
            Require(actual.first == key);
            Require(actual.second == indices);
            Require(std::ranges::equal(plan.Spans()[bucket], indices));
            for (const auto index : indices)
            {
                const auto input = std::ranges::find(inputs, index, &material_graph::ScenePacketGroups::Input::index);
                Require(input != inputs.end());
                const auto record = plan.Templates()[output++];
                Require(record.record == input->record && record.bucket == bucket &&
                        record.capacity == input->capacity);
            }
            ++bucket;
        }
    }
} // namespace

int main()
{
    _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
    _CrtSetReportMode(_CRT_ERROR, _CRTDBG_MODE_FILE);
    _CrtSetReportFile(_CRT_ERROR, _CRTDBG_FILE_STDERR);
    std::set_terminate(
        []
        {
            std::cerr << "TERMINATE remaining=" << material_graph::failPacketGroupRebuild << '\n';
            std::_Exit(86);
        });
    try
    {
        using namespace material_graph;
        std::array inputs{
            ScenePacketGroups::Input{"z", false, 0, 0, 1, 7}, ScenePacketGroups::Input{"a", true, 2, 1, 2, 9},
            ScenePacketGroups::Input{"z", false, 0, 2, 3, 11}, ScenePacketGroups::Input{"a", false, 2, 3, 4, 13}};
        ScenePacketGroups plan;
        Require(!plan.Update(inputs, true));
        Check(plan, inputs);
        const auto* spans = plan.Spans().data();
        const auto* indices = plan.Spans()[0].data();
        const auto* templates = plan.Templates().data();
        ScenePacketRecords records;
        Require(!records.Prepare(plan));
        const auto* storage = records.m_records.data();
        for (uint32_t frame = 0; frame < 64; ++frame)
        {
            Require(plan.Update(inputs, true));
            Require(records.Prepare(plan));
            Require(plan.Spans().data() == spans && plan.Spans()[0].data() == indices &&
                    plan.Templates().data() == templates && records.m_records.data() == storage);
            auto submitted = records.m_records;
            records.m_records[0].visibleOffset = frame + 17;
            Require(submitted[0].visibleOffset != records.m_records[0].visibleOffset);
            Check(plan, inputs);
        }
        const auto mutate = [&](auto change)
        {
            change();
            Require(!plan.Update(inputs, true));
            Require(!records.Prepare(plan));
            Check(plan, inputs);
        };
        mutate([&] { inputs[0].semantic = "changed-cooked-program"; });
        mutate([&] { inputs[1].doubleSided = false; });
        mutate([&] { ++inputs[2].page; });
        mutate([&] { ++inputs[2].record; });
        mutate([&] { ++inputs[3].capacity; });
        Require(!plan.Update(std::span(inputs).first(2), true));
        Check(plan, std::span(inputs).first(2));
        Require(!plan.Update({}, true));
        Require(!records.Prepare(plan));
        Require(records.m_records.empty() && plan.Groups().empty());
        Require(plan.Update({}, true));
        for (size_t frame = 0; frame < 16; ++frame)
        {
            Require(!plan.Update(inputs, false));
            Require(!records.Prepare(plan));
            Check(plan, inputs);
        }
        {
            std::string temporary = "owned-semantic-key";
            const std::array source{ScenePacketGroups::Input{temporary, false, 0, 0, 1, 7}};
            Require(!plan.Update(source, true));
        }
        Require(std::get<0>(plan.Groups()[0].first) == "owned-semantic-key");
        ScenePacketGroups other;
        Require(!other.Update(inputs, true));
        Require(!records.Prepare(other));
        Require(!records.Prepare(plan));
        ScenePacketGroups sameRevision;
        const std::array single{ScenePacketGroups::Input{"different-owner", false, 0, 0, 99, 1}};
        Require(!sameRevision.Update(single, true));
        Require(sameRevision.Revision() == other.Revision());
        Require(!records.Prepare(other));
        Require(!records.Prepare(sameRevision));
        Require(records.m_records.size() == 1 && records.m_records[0].record == 99);
        std::array<ScenePacketGroups::Input, 12> changed;
        for (size_t i = 0; i < changed.size(); ++i)
        {
            changed[i] = {"allocation-failure-program-identity-longer-than-small-string",
                          false,
                          static_cast<uint32_t>(i),
                          i,
                          static_cast<uint32_t>(i + 1),
                          7};
        }
        ScenePacketGroups staged;
        Require(!staged.Update(inputs, true));
        failPacketGroupRebuild = true;
        bool failed{};
        try
        {
            staged.Update(changed, true);
        }
        catch (const std::bad_alloc&)
        {
            failed = true;
        }
        Require(failed);
        Require(!staged.Update(changed, true));
        Check(staged, changed);
        Require(staged.CapacityValid());
        uint64_t total{};
        for (const auto& input : changed)
        {
            total += input.capacity;
        }
        Require(staged.Capacity() == total);
        Require(staged.Capacities().size() == staged.Groups().size());
        auto oversized = inputs;
        oversized[0].capacity = UINT32_MAX;
        Require(!staged.Update(oversized, true));
        Require(!staged.CapacityValid());
        Require(staged.Update(oversized, true));
        Require(!staged.CapacityValid());
        std::cout << "SCENE_PACKET_GROUPS_OK checks=" << checks
                  << " order=legacy-map invalidation=program,state,page,record,capacity,count "
                     "lifetime=owned,submitted-copy\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
