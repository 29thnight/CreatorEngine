#include "AnimationPlayback.h"
#include <mathematics/transform.hpp>

#include <cstdlib>
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
}

int main()
{
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
