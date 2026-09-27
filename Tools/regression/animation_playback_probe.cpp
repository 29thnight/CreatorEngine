#include "AnimationPlayback.h"
#include "../../Engine/SceneRuntime/AnimTaskList.h"
#include "../../Engine/SceneRuntime/TwoBoneIK.h"
#include "../../Engine/RenderEngine/LocalPose.h"
#include "../../Engine/RenderEngine/ClipSamplingCursor.h"
#include <mathematics/transform.hpp>

#include <cstdlib>
#include <cstdint>
#include <iostream>
#include <limits>
#include <string_view>

namespace
{
    int checks = 0;
    void Require(bool condition, std::string_view name)
    {
        ++checks;
        if (!condition)
        {
            std::cerr << "FAIL: " << name << '\n';
            std::exit(1);
        }
    }

    struct Event { double key; int id; };
    std::vector<int> Cross(std::span<const Event> events, double begin,
        double end, bool looping = true)
    {
        std::vector<int> result;
        const auto count = animation::ForEachCrossedEvent(events, begin, end, looping,
            [](const Event& event) { return event.key; },
            [&](const Event& event) { result.push_back(event.id); });
        Require(count == result.size(), "event count matches deliveries");
        return result;
    }

    math::matrix4x4 Translate(float x, float y = 0.f)
    {
        return math::compose(math::vector3{ 1.f, 1.f, 1.f },
            math::quaternion{ 0.f, 0.f, 0.f, 1.f }, math::vector3{ x, y, 0.f });
    }

    bool Near(const math::matrix4x4& a, const math::matrix4x4& b)
    {
        for (int row = 0; row < 4; ++row)
            for (int column = 0; column < 4; ++column)
                if (!std::isfinite(a.m[row][column]) || !std::isfinite(b.m[row][column])
                    || std::abs(a.m[row][column] - b.m[row][column]) > 0.0001f) return false;
        return true;
    }

    void VerifyTwoBoneIK()
    {
        animation::two_bone_solution solution{};
        Require(animation::solve_two_bone_positions({ 0.f, 0.f, 0.f },
            { 0.f, 1.f, 0.f }, { 0.f, 2.f, 0.f },
            { 1.f, 1.f, 0.f }, { 0.f, 0.f, 1.f }, solution),
            "two-bone target solves");
        Require(std::abs(math::length(solution.middle) - 1.f) < .0001f
            && std::abs(math::length(solution.end - solution.middle) - 1.f) < .0001f
            && math::length(solution.end - math::vector3{ 1.f, 1.f, 0.f }) < .0001f,
            "two-bone solver preserves lengths and reaches the target");
        Require(animation::solve_two_bone_positions({ 0.f, 0.f, 0.f },
            { 0.f, 1.f, 0.f }, { 0.f, 2.f, 0.f },
            { 0.f, 9.f, 0.f }, { 1.f, 0.f, 0.f }, solution)
            && std::abs(math::length(solution.end) - 2.f) < .0001f,
            "unreachable target clamps without stretching");
        Require(!animation::solve_two_bone_positions({ 0.f, 0.f, 0.f },
            { 0.f, 0.f, 0.f }, { 0.f, 1.f, 0.f },
            { 1.f, 0.f, 0.f }, { 0.f, 1.f, 0.f }, solution),
            "zero-length chain is rejected");
        Require(animation::solve_two_bone_positions({ 0.f, 0.f, 0.f },
            { 0.f, 1.f, 0.f }, { 0.f, 2.f, 0.f },
            { 0.f, 1.f, 0.f }, { 0.f, 5.f, 0.f }, solution)
            && animation::finite(solution.middle) && animation::finite(solution.end),
            "collinear pole uses a stable fallback plane");
        const auto rotation = animation::rotation_between(
            { 1.f, 0.f, 0.f }, { 0.f, 1.f, 0.f });
        Require(math::length(math::rotate({ 1.f, 0.f, 0.f }, rotation)
            - math::vector3{ 0.f, 1.f, 0.f }) < .0001f,
            "two-bone delta follows engine quaternion order");
        const auto reversed = animation::rotation_between(
            { 1.f, 0.f, 0.f }, { -1.f, 0.f, 0.f });
        Require(math::length(math::rotate({ 1.f, 0.f, 0.f }, reversed)
            - math::vector3{ -1.f, 0.f, 0.f }) < .0001f,
            "opposite bone directions keep a valid bend axis");
    }

    void VerifyQualityRecipe()
    {
        using animation::quality_stage;
        float ikWeight = 1.f;
        for (int frame = 0; frame < 3; ++frame)
        {
            const float next = animation::advance_optional_ik_weight(
                ikWeight, quality_stage::l1, .05f);
            Require(next < ikWeight && next >= 0.f,
                "L1 fades optional IK before omitting its recipe");
            ikWeight = next;
        }
        Require(ikWeight == 0.f, "Optional IK reaches zero before task omission");
        Require(animation::advance_optional_ik_weight(ikWeight,
            quality_stage::l7, .1f) == ikWeight,
            "Offscreen freeze preserves the IK fade state");
        for (int frame = 0; frame < 3; ++frame)
            ikWeight = animation::advance_optional_ik_weight(
                ikWeight, quality_stage::l0, .05f);
        Require(ikWeight == 1.f, "L0 restores optional IK without a weight snap");
        Require(animation::choose_quality({ .02f, false, true, false }) == quality_stage::l7,
            "offscreen animation freezes");
        Require(animation::choose_quality({ .02f, false, true, true }) == quality_stage::l0,
            "editor preview retains full quality");
        Require(animation::choose_quality({ .06f, true, true, false }) == quality_stage::l2,
            "projected height selects additive omission");
        Require(animation::choose_quality({ .04f, true, true, false }) == quality_stage::l3,
            "small visible animation omits masked overlays");
        Require(animation::choose_quality({ .03f, true, true, false }) == quality_stage::l4,
            "tiny visible animation omits crossfades");
        Require(animation::choose_quality({ .015f, true, true, false }) == quality_stage::l5,
            "authored tiny animation can use a bone prefix");
        const auto makeRecipe = []
        {
            animation::task_list tasks;
            auto append = [&](animation::task_kind kind, std::uint32_t previous,
                std::uint32_t slot)
            {
                auto item = animation::make_task(kind);
                item.dependency_a = previous;
                item.output_slot = slot;
                return tasks.append(item);
            };
            auto previous = append(animation::task_kind::prepare_composite,
                animation::invalid_task, animation::invalid_task);
            previous = append(animation::task_kind::sample_clip, previous, 0);
            previous = append(animation::task_kind::materialize, previous, 0);
            previous = append(animation::task_kind::blend_masked, previous, 0);
            previous = append(animation::task_kind::sample_clip, previous, 1);
            previous = append(animation::task_kind::materialize, previous, 1);
            previous = append(animation::task_kind::make_additive, previous, 1);
            previous = append(animation::task_kind::apply_additive, previous, 1);
            previous = append(animation::task_kind::sample_clip, previous, 2);
            previous = append(animation::task_kind::materialize, previous, 2);
            previous = append(animation::task_kind::blend_masked, previous, 2);
            tasks.set_output(append(animation::task_kind::output, previous,
                animation::invalid_task));
            return tasks;
        };
        auto count = [](animation::task_list& tasks)
        {
            std::size_t reached = 0;
            tasks.for_each_reachable([&](const animation::task&) { ++reached; });
            return reached;
        };
        auto full = makeRecipe();
        Require(count(full) == 12, "full layered recipe remains reachable");
        auto l2 = makeRecipe();
        l2.prune_overlay_layers(animation::quality_stage::l2);
        Require(count(l2) == 8, "L2 prunes additive sample and apply tasks");
        auto l3 = makeRecipe();
        l3.prune_overlay_layers(animation::quality_stage::l3);
        Require(count(l3) == 5, "L3 also prunes masked overlay while keeping base");

        animation::task_list transition;
        auto append = [&](animation::task_kind kind, std::uint32_t previous,
            std::uint32_t slot)
        {
            auto item = animation::make_task(kind);
            item.dependency_a = previous;
            item.output_slot = slot;
            return transition.append(item);
        };
        auto predecessor = append(animation::task_kind::prepare_composite,
            animation::invalid_task, animation::invalid_task);
        predecessor = append(animation::task_kind::sample_clip, predecessor, 0);
        predecessor = append(animation::task_kind::materialize, predecessor, 0);
        predecessor = append(animation::task_kind::blend_masked, predecessor, 0);
        const auto current = append(animation::task_kind::sample_clip, predecessor, 1);
        auto nextTask = animation::make_task(animation::task_kind::sample_clip);
        nextTask.dependency_a = current;
        nextTask.output_slot = 1;
        nextTask.sample_slot = 1;
        const auto next = transition.append(nextTask);
        auto blend = animation::make_task(animation::task_kind::blend);
        blend.dependency_a = current;
        blend.dependency_b = next;
        predecessor = transition.append(blend);
        predecessor = append(animation::task_kind::materialize, predecessor, 1);
        predecessor = append(animation::task_kind::make_additive, predecessor, 1);
        predecessor = append(animation::task_kind::apply_additive, predecessor, 1);
        transition.set_output(append(animation::task_kind::output, predecessor,
            animation::invalid_task));
        transition.prune_overlay_layers(animation::quality_stage::l2);
        Require(count(transition) == 5,
            "L2 removes both transition samples and blend with additive overlay");

        animation::task_list snapped;
        const auto currentSample = snapped.append(animation::make_task(animation::task_kind::sample_clip));
        const auto nextSample = snapped.append(animation::make_task(animation::task_kind::sample_clip));
        auto snappedBlend = animation::make_task(animation::task_kind::blend);
        snappedBlend.dependency_a = currentSample;
        snappedBlend.dependency_b = nextSample;
        const auto blendIndex = snapped.append(snappedBlend);
        auto snappedOutput = animation::make_task(animation::task_kind::output);
        snappedOutput.dependency_a = blendIndex;
        snapped.set_output(snapped.append(snappedOutput));
        Require(snapped.snap_single_blend(false), "L4 snaps the blend recipe");
        std::size_t recipeNodes = 0;
        std::size_t skippedNodes = 0;
        snapped.for_each_task_snapshot([&](std::uint32_t index,
            const animation::task&, bool reachable)
        {
            Require(index == recipeNodes++, "S7 task snapshot keeps recipe indices");
            skippedNodes += !reachable ? 1 : 0;
        });
        Require(recipeNodes == 4 && skippedNodes == 2 && count(snapped) == 2,
            "S7 snapshot retains unreachable tasks beside the executed recipe");
    }

    void VerifyLocalPose()
    {
        const Animation::LocalTransform a{ { 1.f, 2.f, 3.f },
            math::quaternion{ 0.f, 0.f, 0.f, 1.f }, { 1.f, 2.f, 3.f } };
        const Animation::LocalTransform b{ { 5.f, 6.f, 7.f },
            math::quaternion{ 0.f, 0.f, 1.f, 0.f }, { 3.f, 4.f, 5.f } };
        Require(Animation::Blend(a, b, 0.f).ToMatrix() == a.ToMatrix(), "exact TRS start");
        Require(Animation::Blend(a, b, 1.f).ToMatrix() == b.ToMatrix(), "exact TRS end");
        const auto half = Animation::Blend(a, b, 0.5f);
        const math::matrix4x4 expectedHalf{ 0.f, 2.f, 0.f, 0.f,
            -3.f, 0.f, 0.f, 0.f, 0.f, 0.f, 4.f, 0.f, 3.f, 4.f, 5.f, 1.f };
        Require(Near(half.ToMatrix(), expectedHalf), "analytic 90 degree TRS with nonuniform scale");
        Require(std::abs(math::dot(half.m_rotation, half.m_rotation) - 1.f) < 0.00001f,
            "blended rotation remains unit length");
        auto antipodal = b;
        antipodal.m_rotation = -antipodal.m_rotation;
        Require(Near(Animation::Blend(b, antipodal, 0.5f).ToMatrix(), b.ToMatrix()),
            "opposite quaternion signs represent one rotation");

        auto mirrored = a;
        mirrored.m_scale = { -1.f, -2.f, -3.f };
        const auto collapsed = Animation::Blend(a, mirrored, 0.5f);
        Require(collapsed.m_scale == math::vector3{}, "signed scale interpolates through zero");
        Require(Near(collapsed.ToMatrix(), math::matrix4x4{
            0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f,
            0.f, 0.f, 0.f, 0.f, 1.f, 2.f, 3.f, 1.f }), "zero scale retains translation");
        Require(Near(Animation::Blend(collapsed, a, 0.5f).ToMatrix(),
            math::compose({ 0.5f, 1.f, 1.5f }, a.m_rotation, a.m_translation)),
            "zero scale can blend out instead of freezing after failed decomposition");

        Require(Animation::BlendMasked(a, b, 0.f).ToMatrix() == a.ToMatrix(),
            "zero mask retains the base pose");
        Require(Animation::BlendMasked(a, b, 1.f).ToMatrix() == b.ToMatrix(),
            "full mask selects the layer exactly");
        Require(Near(Animation::BlendMasked(a, b, 0.5f).ToMatrix(), expectedHalf),
            "partial mask blends local TRS without matrix decomposition");
        Require(Animation::BlendMasked(a, b, std::numeric_limits<float>::quiet_NaN()).ToMatrix()
            == a.ToMatrix(), "invalid mask weight cannot poison a pose");

        const auto delta = Animation::MakeAdditive(b, a);
        Require(Near(Animation::ApplyAdditive(a, delta, 0.f).ToMatrix(), a.ToMatrix()),
            "zero additive weight retains base");
        Require(Near(Animation::ApplyAdditive(a, delta, 1.f).ToMatrix(), b.ToMatrix()),
            "full additive delta reconstructs source");
        const auto additiveHalf = Animation::ApplyAdditive(a, delta, 0.5f);
        Require(Near(additiveHalf.ToMatrix(), expectedHalf),
            "half additive delta blends translation rotation and signed scale");

        auto reference = a;
        reference.m_rotation = math::normalize(math::quaternion{ 0.3f, 0.f, 0.f, 0.95f });
        const auto authoredDelta = math::normalize(math::quaternion{ 0.f, 0.4f, 0.f, 0.92f });
        auto source = reference;
        source.m_rotation = reference.m_rotation * authoredDelta;
        const auto rotatedDelta = Animation::MakeAdditive(source, reference);
        auto independentBase = b;
        independentBase.m_rotation = math::normalize(math::quaternion{ 0.f, 0.f, 0.5f, 0.87f });
        const auto applied = Animation::ApplyAdditive(independentBase, rotatedDelta, 1.f);
        Require(Near(applied.ToMatrix(), math::compose(independentBase.m_scale,
            independentBase.m_rotation * authoredDelta, independentBase.m_translation)),
            "additive rotation composes relative to an independent base");

        // Independent old matrix path over positive scales and changing axes.
        for (int sample = 0; sample < 32; ++sample)
        {
            auto from = a;
            auto to = b;
            const float angle = sample * 0.13f;
            from.m_rotation = { std::sin(angle), 0.f, 0.f, std::cos(angle) };
            to.m_rotation = { 0.f, std::sin(angle * 0.7f), 0.f, std::cos(angle * 0.7f) };
            const auto oldFrom = math::decompose(from.ToMatrix());
            const auto oldTo = math::decompose(to.ToMatrix());
            Require(oldFrom.has_value() && oldTo.has_value(), "reference positive TRS decomposes");
            for (float alpha : { 0.f, 0.25f, 0.5f, 0.75f, 1.f })
                Require(Near(Animation::Blend(from, to, alpha).ToMatrix(), math::compose(
                    math::lerp(oldFrom->scale, oldTo->scale, alpha),
                    math::slerp(oldFrom->rotation, oldTo->rotation, alpha),
                    math::lerp(oldFrom->translation, oldTo->translation, alpha))),
                    "TRS blend matches old matrix reference for positive scales");
        }

        Animation::LocalPose pose;
        pose.Resize(3);
        Require(pose.GetTransform(2).ToMatrix() == math::matrix4x4::identity(), "new pose slots are identity");
        pose.SetTransform(1, half);
        auto copy = pose;
        pose.SetTransform(1, a);
        Require(Near(copy.GetTransform(1).ToMatrix(), expectedHalf), "pose values own independent storage");
        const auto* translations = pose.GetTranslations().data();
        const auto* rotations = pose.GetRotations().data();
        const auto* scales = pose.GetScales().data();
        pose.Clear();
        Require(pose.GetCount() == 0, "cleared pose has no live bones");
        pose.Resize(3);
        Require(pose.GetTranslations().data() == translations && pose.GetRotations().data() == rotations
            && pose.GetScales().data() == scales, "clearing and resizing reuses all SoA allocations");
        Require(pose.GetTransform(1).ToMatrix() == math::matrix4x4::identity(), "rebind cannot retain previous TRS");
    }

    void VerifyKeyCursor()
    {
        struct Key { double time; };
        const auto reference = [](std::span<const Key> keys, double time)
        {
            std::size_t index = 0;
            while (index + 2 < keys.size() && keys[index + 1].time < time) ++index;
            return index;
        };
        std::uint32_t random = 0x13579bdu;
        for (std::size_t count : { 0u, 1u, 2u, 3u, 17u, 128u })
        {
            std::vector<Key> keys;
            for (std::size_t i = 0; i < count; ++i)
                keys.push_back({ static_cast<double>(i * i) * 0.125 });
            std::size_t cursor = (std::numeric_limits<std::size_t>::max)();
            const auto check = [&](double time)
            {
                Require(Animation::FindKeyInterval(std::span<const Key>{ keys }, time, cursor)
                    == reference(keys, time), "cursor matches linear reference across seeks and boundaries");
            };
            for (const auto& key : keys)
            {
                check(std::nextafter(key.time, -INFINITY));
                check(key.time);
                check(std::nextafter(key.time, INFINITY));
                check(key.time); // paused at an exact key
            }
            for (auto it = keys.rbegin(); it != keys.rend(); ++it)
            { check(it->time); check(it->time - 0.001); }
            for (int i = 0; i < 1000; ++i)
            {
                random = random * 1664525u + 1013904223u;
                check((static_cast<double>(random % 10001u) / 10000.0)
                    * (keys.empty() ? 1.0 : keys.back().time + 2.0) - 1.0);
            }
            check(-INFINITY); check(INFINITY); check(std::numeric_limits<double>::quiet_NaN());
        }
        const Key duplicateTimes[] = { { 0.0 }, { 1.0 }, { 1.0 }, { 2.0 }, { 2.0 } };
        for (std::size_t initial = 0; initial < 6; ++initial)
        {
            std::size_t cursor = initial;
            for (double time : { 2.0, 1.0, 1.01, 2.01, -1.0, 0.0 })
                Require(Animation::FindKeyInterval(std::span<const Key>{ duplicateTimes }, time, cursor)
                    == reference(duplicateTimes, time), "duplicate timestamps keep the old interval rule");
        }

        // Count comparisons, not wall time: a warm lookup must stay constant,
        // and a long seek must not quietly regress to scanning thousands of keys.
        struct CountedTime
        {
            double m_value; std::size_t* m_reads;
            operator double() const noexcept { ++*m_reads; return m_value; }
        };
        struct CountedKey { CountedTime time; };
        std::size_t reads = 0;
        std::vector<CountedKey> many;
        for (int i = 0; i < 8192; ++i) many.push_back({ { static_cast<double>(i), &reads } });
        std::size_t cursor = 4000;
        const auto seek = [&](double time, std::size_t expected, std::size_t maxReads)
        {
            reads = 0;
            const auto actual = Animation::FindKeyInterval(std::span<const CountedKey>{ many }, time, cursor);
            Require(actual == expected && reads <= maxReads, "bounded cursor/seek comparison count");
        };
        seek(4000.5, 4000, 2); seek(4001.5, 4001, 4); seek(4000.5, 4000, 6);
        seek(8000.5, 8000, 24); seek(12.5, 12, 24);

        Animation::ClipSamplingCursor clip;
        auto tracks = clip.Prepare(3, 54);
        tracks[0].m_translation = 12;
        const auto* storage = tracks.data();
        Require(clip.Prepare(3, 54)[0].m_translation == 12, "same clip retains key cursors");
        Require(clip.Prepare(4, 54)[0].m_translation == 0, "clip change resets key cursors");
        clip.Clear();
        Require(clip.GetTracks().empty() && clip.GetClipIndex() == -1, "generation unbind invalidates cursors");
        Require(clip.Prepare(4, 54).data() == storage, "rebind reuses cursor allocation");
    }
}

int main()
{
    VerifyQualityRecipe();
    VerifyTwoBoneIK();
    VerifyLocalPose();
    VerifyKeyCursor();
    const Event authored[] = { { .75, 75 }, { 0., 0 }, { .25, 25 },
        { 1., 100 }, { .25, 26 }, { -1., -1 }, { 2., -2 },
        { std::numeric_limits<double>::quiet_NaN(), -3 } };
    Require(Cross(authored, .2, 1.3) == std::vector<int>{25, 26, 75, 100, 0, 25, 26},
        "forward wrap is chronological with stable ties and both boundary keys");
    Require(Cross(authored, .3, -.3) == std::vector<int>{25, 26, 0, 100, 75},
        "reverse wrap is chronological");
    Require(Cross(authored, .25, .25).empty(), "zero speed is not a full loop");
    Require(Cross(authored, 0., 1.) == std::vector<int>{25, 26, 75, 100, 0},
        "exact full loop retains end and next start");
    Require(Cross(authored, 0., 1., false) == std::vector<int>{25, 26, 75, 100},
        "nonlooping terminal step");
    Require(Cross(authored, 1., 1., false).empty(), "terminal hold does not repeat");
    Require(Cross(authored, .1, .25) == std::vector<int>{25, 26}, "inclusive step end");
    Require(Cross(authored, .25, .5).empty(), "exclusive step start");
    Require(Cross(authored, .5, .25) == std::vector<int>{25, 26}, "reverse inclusive end");
    Require(Cross(authored, 1., 0.) == std::vector<int>{75, 25, 26, 0, 100},
        "exact reverse boundary");
    Require(Cross(authored, 0., std::numeric_limits<double>::infinity()).empty(),
        "nonfinite interval rejected");
    Require(Cross({}, 0., 4.).empty(), "empty event list");

    std::vector<std::size_t> eventOrder;
    std::vector<int> delivered;
    const auto deliver = [&](const Event& event) { delivered.push_back(event.id); };
    const auto keyOf = [](const Event& event) { return event.key; };
    animation::ForEachCrossedEvent(std::span<const Event>{ authored }, .2, 1.3,
        true, eventOrder, keyOf, deliver);
    const auto* orderStorage = eventOrder.data();
    Require(delivered == Cross(authored, .2, 1.3),
        "reused event-order scratch preserves delivery order");
    delivered.clear();
    animation::ForEachCrossedEvent(std::span<const Event>{ authored }, .3, -.3,
        true, eventOrder, keyOf, deliver);
    Require(eventOrder.data() == orderStorage && delivered == Cross(authored, .3, -.3),
        "reverse event traversal reuses ordering storage");

    const Event repeated[] = { { .25, 1 }, { .75, 2 } };
    Require(Cross(repeated, .1, 3.1) == std::vector<int>{1, 2, 1, 2, 1, 2},
        "three skipped cycles retain every occurrence");
    // An integration invariant: splitting one advance across frames changes
    // neither the sequence nor the count, even at exact loop boundaries.
    auto split = Cross(authored, .2, 1.);
    const auto tail = Cross(authored, 0., 1.3);
    split.insert(split.end(), tail.begin(), tail.end());
    Require(split == Cross(authored, .2, 2.3), "frame partition invariance");

    const auto held = animation::AdvanceClip(5., 0., 10., true);
    Require(held.time == 5.f && held.progress == .5f && held.eventBegin == held.eventEnd,
        "held clock");
    const auto loops = animation::AdvanceClip(5., 20., 10., true);
    Require(loops.time == 5.f && loops.progress == .5f
        && loops.eventBegin == .5 && loops.eventEnd == 2.5, "unwrapped event clock");
    Require(Cross(repeated, loops.eventBegin, loops.eventEnd).size() == 4,
        "clock retains exact two-loop event multiplicity");
    const auto reverse = animation::AdvanceClip(2., -5., 10., true);
    Require(reverse.time == 7.f && reverse.eventBegin == .2 && reverse.eventEnd == -.3,
        "reverse sample time wraps positive");
    const auto terminal = animation::AdvanceClip(9., 3., 10., false);
    Require(terminal.time == 10.f && terminal.progress == 1.f && terminal.eventEnd == 1.,
        "nonlooping clock clamps");
    const auto emptyClip = animation::AdvanceClip(3., 2., 0., true);
    Require(emptyClip.time == 0.f && emptyClip.eventBegin == emptyClip.eventEnd,
        "zero-duration clip cannot divide by zero or emit");
    const auto invalid = animation::AdvanceClip(0.,
        std::numeric_limits<double>::infinity(), 10., true);
    Require(invalid.time == 0.f && invalid.eventBegin == invalid.eventEnd,
        "invalid step cannot poison the pose clock");

    const auto previous = Translate(2.f);
    const auto layerA = Translate(4.f);
    const auto layerB = Translate(8.f);
    animation::LayerLocalPose selected(previous);
    selected.Apply(layerA, true, true, false);
    Require(selected.Local() == previous, "all masks excluded retains previous local");
    selected.Apply(layerB, false, true, true);
    Require(selected.Local() == previous, "disabled controller cannot leak stale pose");
    selected.Apply(layerB, true, false, true);
    Require(selected.Local() == previous, "missing channel cannot overwrite pose");
    const auto parent = Translate(0.f, 3.f);
    Require(selected.Local() * parent == Translate(2.f, 3.f),
        "excluded child still follows animated parent");
    selected.Apply(layerA, true, true, true);
    Require(selected.Local() == layerA, "allowed layer replaces local");
    selected.Apply(layerB, true, true, true);
    Require(selected.Local() == layerB, "last enabled authored layer wins");
    const auto palette = math::matrix4x4::identity() * selected.Local() * parent;
    Require(palette == Translate(8.f, 3.f), "projection and palette consume same local");

    std::cout << "ANIMATION_PLAYBACK_OK checks=" << checks << '\n';
}
