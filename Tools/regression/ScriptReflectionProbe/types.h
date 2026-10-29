#pragma once
// Small owner-independent objects exercise the production reflection protocol.
// The engine's own ce::string serializers are used, including strict UTF-16/UTF-8 conversion.
#include "../../../Engine/Utility_Framework/Reflection/ScriptReflectionPrimitives.h"
#include "../../../Engine/Utility_Framework/Reflection/ScriptReflectionMath.h"
#include "../../../Engine/Utility_Framework/Reflection/ScriptReflectionOwnership.h"
#include "../../../Engine/Utility_Framework/Reflection/ScriptReflectionContainers.h"
#include "../../../Engine/SceneRuntime/BlackBoard.h"
#include "../../../Engine/SceneRuntime/StateMachineChildren.h"
#include <array>
#include <expected>
#include <map>
#include <optional>
#include <set>
#include <span>
#include <string>
#include <tuple>
#include <utility>
#include <variant>
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

    struct [[reflgen::reflect("ScriptComponent"), reflgen::public_methods]] Script
    {
        std::string m_scriptType = "ProbeScript";
        std::vector<std::string> m_fieldData{"one", "two"};
        bool enabled = true;
        bool IsEnabled() const { return enabled; }
        void SetEnabled(bool value) { enabled = value; }
        int GetInstanceId() const { return 101; }
        bool HasInstance() const { return true; }
        std::uint64_t InitializationAttempts() const { return 1; }
    };

    struct [[reflgen::reflect("InvalidScriptComponent"), reflgen::public_methods]] InvalidScript
    {
        std::string m_errorMessage = "missing probe script";
        bool enabled = true;
        bool IsEnabled() const { return enabled; }
        void SetEnabled(bool value) { enabled = value; }
    };

    struct [[reflgen::reflect("ProbeComponent"), reflgen::public_methods]] Component : Base
    {
        using GraphValue = std::variant<std::monostate, bool, long long, double,
            ce::fixed_array<double, 3>, ce::fixed_array<double, 4>, std::string>;
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
        const Settings* missingSettings = nullptr;
        std::vector<Settings> items{Settings{}, Settings{21, 0.5f, "second"}};
        std::map<std::string, Settings> named{{"alpha", Settings{}}, {"키", Settings{31, 0.75f, "map"}}};
        std::optional<Settings> optional{Settings{}};
        std::optional<Settings> absent;
        std::array<int, 3> fixed{1, 2, 3};
        std::set<int> readonlySet{3, 5};
        std::vector<bool> bits{true, false};
        std::tuple<int, Settings, std::string> tuple{5, Settings{}, "tuple"};
        std::variant<Settings, int> choice{Settings{33, 0.5f, "choice"}};
        std::expected<int, Settings> outcome{std::unexpected(Settings{44, 0.75f, "error"})};
        Mode mode = Mode::Running;
        const int constant = 19;
        [[reflgen::readonly]] Settings locked;
        Cycle cycle;
        int calls = 0;
        mutable int lookupCalls = 0;
        [[reflgen::ignore]] ce::owned_func<void(float)> tickCallback;
        [[reflgen::ignore]] FSM::StateMachineChildren stateMachine;

        [[reflgen::reflect]] int Increase(int amount) { ++calls; settings.count += amount; return settings.count; }
        [[reflgen::reflect]] void Adjust(int& value) const { value += settings.count; }
        [[reflgen::reflect]] std::string Echo(const std::string& text) const { return text; }
        [[reflgen::reflect]] math::vector3 EchoDirection(math::vector3 value) const { return value; }
        [[reflgen::reflect]] Aliased EchoAliased(Aliased value) const { return value; }
        [[reflgen::reflect]] int Choose(int value) const { return value + 10; }
        [[reflgen::reflect]] int Choose(float value) const { return static_cast<int>(value) + 20; }
        [[reflgen::reflect]] int ChooseSpan(std::span<int> values) const { return 100 + static_cast<int>(values.size()); }
        [[reflgen::reflect]] int ChooseSpan(std::span<const int> values) const { return 200 + static_cast<int>(values.size()); }
        [[reflgen::reflect]] int GraphAlternative(GraphValue value) const { return static_cast<int>(value.index()); }
        [[reflgen::reflect]] int FillSpan(std::span<int> values) const
        {
            for (auto& value : values) value += 10;
            return static_cast<int>(values.size());
        }
        [[reflgen::reflect]] void FillSettings(std::span<Settings> values) const
        {
            for (auto& value : values) ++value.count;
        }
        [[reflgen::reflect]] Settings& GetSettings() { return settings; }
        [[reflgen::reflect]] const Settings& GetReadOnlySettings() const { return settings; }
        [[reflgen::reflect]] Settings SnapshotSettings() const { return settings; }
        [[reflgen::reflect]] Settings* CreateSettings(int count)
        {
            ++calls;
            shared = own::make_shared<Settings>();
            shared->count = count;
            return shared.borrow().unsafe_get();
        }
        [[reflgen::reflect]] int ReadNativeSettings(const Settings* value) const
        {
            return value ? value->count : -1;
        }
        [[reflgen::reflect]] void UpdateNativeSettings(Settings* value, int count)
        {
            ++calls;
            if (value) value->count = count;
        }
        [[reflgen::reflect]] void RemoveNativeSettings(Settings* value)
        {
            ++calls;
            if (shared.borrow().unsafe_get() == value) shared.reset();
        }
        [[reflgen::reflect]] void BindSharedSettings(own::shared_owner<Settings> value)
        {
            ++calls;
            shared = std::move(value);
        }
        [[reflgen::reflect]] own::shared_owner<Settings> GetShared() const { return shared; }
        [[reflgen::reflect, reflgen::script_once]] own::shared_owner<Settings> TakeShared()
        {
            ++calls;
            return std::exchange(shared, {});
        }
        [[reflgen::reflect]] void ResetShared() { shared = own::make_shared<Settings>(); }
        [[reflgen::reflect]] ce::string_view TitleView() const { return title.view(); }
        [[reflgen::reflect]] std::string EchoCString(const char* text) const { return std::string(text); }
        [[reflgen::reflect]] std::string EchoView(std::string_view text) const { return std::string(text); }
        [[reflgen::reflect]] void ResizeItems(int size) { items.resize(static_cast<std::size_t>(size)); }
        [[reflgen::reflect]] void EraseNamed(const std::string& key) { named.erase(key); }
        [[reflgen::reflect]] void ClearOptional() { optional.reset(); }
        [[reflgen::reflect]] void SwitchChoice() { choice = 91; }
        [[reflgen::reflect]] void SucceedOutcome() { outcome = 92; }
        [[reflgen::reflect]] const Settings* FindSettings(const std::string& name) const
        {
            ++lookupCalls;
            const auto found = named.find(name);
            return found == named.end() ? nullptr : &found->second;
        }
        [[reflgen::reflect]] void RawArgument(int* value) { if (value) ++*value; }
        [[reflgen::reflect]] void UnsafeRecord(Cycle value) { ++calls; if (value.next) ++value.next->value; }
        [[reflgen::reflect]] void SetTickCallback(ce::owned_func<void(float)> callback)
        { tickCallback = std::move(callback); }
        [[reflgen::reflect]] void TriggerTick(float amount) const { if (tickCallback) tickCallback(amount); }
        [[reflgen::reflect]] void ClearTickCallback() { tickCallback = ce::owned_func<void(float)>{}; }
        [[reflgen::reflect]] void RunVoidCallback(ce::owned_func<void()> callback) const { callback(); }
        [[reflgen::reflect]] void RunVectorCallback(ce::owned_func<void(math::vector2)> callback) const
        { callback(math::vector2{3.0f, -4.0f}); }
        [[reflgen::reflect]] void RunTextCallback(const ce::owned_func<void(const std::string&)>& callback,
                                                 const std::string& text) const { callback(text); }
        [[reflgen::reflect]] bool RunConditionCallback(ConditionFunc callback) const
        {
            BlackBoard board;
            // Populate the actual authoring value map without linking scene/asset lookup.
            auto& values = const_cast<ce::engine_node_map<std::string, BlackBoardValue>&>(board.GetValues());
            BlackBoardValue ready{};
            ready.Type = BlackBoardType::Bool;
            ready.BoolValue = true;
            values["ready"] = ready;
            return callback(board);
        }
        [[reflgen::reflect]] FSM::FSMState* AddState(const std::string& name)
        {
            auto* result = stateMachine.AddState(name);
            ++calls;
            return result;
        }
        [[reflgen::reflect]] void RemoveState(FSM::FSMState* state)
        {
            stateMachine.RemoveState(state);
            ++calls;
        }
        [[reflgen::reflect]] FSM::Transition* AddTransition(FSM::FSMState* from, FSM::FSMState* to,
                                                          ConditionFunc condition)
        {
            auto* result = stateMachine.AddTransition(from, to, std::move(condition));
            ++calls;
            return result;
        }
        [[reflgen::reflect]] void RemoveTransition(FSM::Transition* transition)
        {
            stateMachine.RemoveTransition(transition);
            ++calls;
        }
        [[reflgen::reflect]] bool EvaluateTransition(FSM::Transition* transition) const
        {
            if (!transition || stateMachine.IdentifyTransition(transition) == 0)
                throw std::invalid_argument("transition does not belong to the fixture state machine");
            BlackBoard board;
            auto& values = const_cast<ce::engine_node_map<std::string, BlackBoardValue>&>(board.GetValues());
            BlackBoardValue ready{};
            ready.Type = BlackBoardType::Bool;
            ready.BoolValue = true;
            values["ready"] = ready;
            return (*transition)(board);
        }
        [[reflgen::reflect]] FSM::FSMState* FindStateByName(const std::string& name) const
        {
            ++lookupCalls;
            return stateMachine.FindStateByName(name);
        }
        [[reflgen::reflect]] const Component& GetReadOnlySelf() const { return *this; }
    };
}

// The provider explicitly approves this lookup's borrowed pointer, mirroring
// the engine's reflected-object pointer policy. Arbitrary pointer inputs remain
// non-deserializable and the selected object can never be made writable.
namespace reflgen
{
    // Match the production raw reflected-record policy without pulling the
    // unrelated authoring/YAML adapters into this portable runtime fixture.
    template<class T> requires (std::is_same_v<std::remove_cv_t<T>, FSM::FSMState>
                            || std::is_same_v<std::remove_cv_t<T>, FSM::Transition>)
    struct runtime_value_traits<T*>
    {
        static constexpr value_shape shape = value_shape::pointer;
        using children = type_list<T>;
        static constexpr bool mutable_borrow = !std::is_const_v<T>;
        static constexpr bool retained_borrow = true;
        static constexpr bool serializable = false;
        static constexpr bool deserializable = false;
        static constexpr bool assignment_safe = false;
        static const T* get(T* const& value) noexcept { return value; }
        static T* get(T*& value) noexcept { return value; }
    };
    template<> struct runtime_value_traits<reflection_probe::Settings*>
    {
        static constexpr value_shape shape = value_shape::pointer;
        using children = type_list<reflection_probe::Settings>;
        static constexpr bool mutable_borrow = true;
        static constexpr bool retained_borrow = true;
        static constexpr bool serializable = false;
        static constexpr bool deserializable = false;
        static constexpr bool assignment_safe = false;
        static const reflection_probe::Settings* get(reflection_probe::Settings* const& value) { return value; }
        static reflection_probe::Settings* get(reflection_probe::Settings*& value) { return value; }
    };
    template<> struct runtime_value_traits<const reflection_probe::Settings*>
    {
        static constexpr value_shape shape = value_shape::pointer;
        using children = type_list<reflection_probe::Settings>;
        static constexpr bool mutable_borrow = false;
        static constexpr bool deserializable = false;
        static constexpr bool assignment_safe = false;
        static const reflection_probe::Settings* get(const reflection_probe::Settings* const& value) { return value; }
    };
}
