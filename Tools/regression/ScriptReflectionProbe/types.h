#pragma once
// Small owner-independent objects exercise the production reflection protocol.
// The engine's own ce::string serializers are used, including strict UTF-16/UTF-8 conversion.
#include "../../../Engine/Utility_Framework/Reflection/ScriptReflectionPrimitives.h"
#include "../../../Engine/Utility_Framework/Reflection/ScriptReflectionMath.h"
#include "../../../Engine/Utility_Framework/Reflection/ScriptReflectionOwnership.h"
#include "../../../Engine/Utility_Framework/Reflection/ScriptReflectionContainers.h"
#include <array>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <vector>

namespace reflection_probe
{
    enum class [[reflgen::reflect]] Mode { Idle = 0, Running = 7 };

    struct [[reflgen::reflect("ProbeSettings"), reflgen::public_methods]] Settings
    {
        int count = 7;
        float gain = 0.25f;
        std::string label = "nested";

        [[reflgen::reflect]] int Add(int amount) { count += amount; return count; }
        [[reflgen::reflect]] int ReadCount() const { return count; }
        // C++ constness alone is insufficient: the reflection capability must
        // remain read-only even when an unusual const method returns T&.
        [[reflgen::reflect]] Settings& MutableSelf() const { return const_cast<Settings&>(*this); }
    };

    struct [[reflgen::reflect("ProbeBase"), reflgen::public_methods]] Base
    {
        int inherited = 11;
        [[reflgen::reflect]] int BaseValue() const { return inherited; }
    };

    struct [[reflgen::reflect("ProbeCycle"), reflgen::public_methods]] Cycle
    {
        int value = 9;
        Cycle* next = nullptr;
    };

    struct [[reflgen::reflect("ProbeAliased")]] Aliased
    {
        [[reflgen::serialized_name("wire_count")]] int count = 17;
    };

    // These minimal records let the actual production F# wrappers exercise
    // their captured component identity through the real reflection service.
    struct [[reflgen::reflect("PhysicsBodyComponent")]] PhysicsBody
    {
        int value = 61;
    };
    struct [[reflgen::reflect("CharacterMovementComponent")]] CharacterMovement
    {
        int value = 71;
    };

    struct [[reflgen::reflect("ProbeComponent"), reflgen::public_methods]] Component : Base
    {
        ce::string title{u"한글 \U0001F3B9"};
        std::string utf8 = "hello";
        ce::utf8_string engineUtf8{"engine UTF-8"};
        Settings settings;
        Aliased aliased;
        math::vector3 direction{1.0f, 2.0f, 3.0f};
        [[reflgen::readonly]] math::vector3 lockedDirection{4.0f, 5.0f, 6.0f};
        ce::dynamic_array<Settings> engineItems{Settings{41, 0.5f, "engine item"}};
        ce::basic_flat_hash_map<ce::string, Settings> engineNamed = [] {
            ce::basic_flat_hash_map<ce::string, Settings> values;
            values.try_emplace(ce::string(u"엔진"), Settings{51, 0.5f, "engine map"});
            return values;
        }();
        own::shared_owner<Settings> shared = own::make_shared<Settings>();
        [[reflgen::readonly]] own::shared_owner<Settings> lockedShared = own::make_shared<Settings>();
        std::vector<Settings> items{Settings{}, Settings{21, 0.5f, "second"}};
        std::map<std::string, Settings> named{{"alpha", Settings{}}, {"키", Settings{31, 0.75f, "map"}}};
        std::optional<Settings> optional{Settings{}};
        std::optional<Settings> absent;
        std::array<int, 3> fixed{1, 2, 3};
        std::set<int> readonlySet{3, 5};
        std::vector<bool> bits{true, false};
        Mode mode = Mode::Running;
        const int constant = 19;
        [[reflgen::readonly]] Settings locked;
        Cycle cycle;
        int calls = 0;

        [[reflgen::reflect]] int Increase(int amount) { ++calls; settings.count += amount; return settings.count; }
        [[reflgen::reflect]] void Adjust(int& value) const { value += settings.count; }
        [[reflgen::reflect]] std::string Echo(const std::string& text) const { return text; }
        [[reflgen::reflect]] math::vector3 EchoDirection(math::vector3 value) const { return value; }
        [[reflgen::reflect]] Aliased EchoAliased(Aliased value) const { return value; }
        [[reflgen::reflect]] int Choose(int value) const { return value + 10; }
        [[reflgen::reflect]] int Choose(float value) const { return static_cast<int>(value) + 20; }
        [[reflgen::reflect]] Settings& GetSettings() { return settings; }
        [[reflgen::reflect]] const Settings& GetReadOnlySettings() const { return settings; }
        [[reflgen::reflect]] Settings SnapshotSettings() const { return settings; }
        [[reflgen::reflect]] own::shared_owner<Settings> GetShared() const { return shared; }
        [[reflgen::reflect]] void ResetShared() { shared = own::make_shared<Settings>(); }
        [[reflgen::reflect]] ce::string_view TitleView() const { return title.view(); }
        [[reflgen::reflect]] std::string EchoCString(const char* text) const { return std::string(text); }
        [[reflgen::reflect]] std::string EchoView(std::string_view text) const { return std::string(text); }
        [[reflgen::reflect]] void ResizeItems(int size) { items.resize(static_cast<std::size_t>(size)); }
        [[reflgen::reflect]] void EraseNamed(const std::string& key) { named.erase(key); }
        [[reflgen::reflect]] void ClearOptional() { optional.reset(); }
        [[reflgen::reflect]] void RawArgument(int* value) { if (value) ++*value; }
    };
}
