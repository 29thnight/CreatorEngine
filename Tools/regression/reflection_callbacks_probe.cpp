#include "../../Engine/Containers/Creator.Containers.h"
#include "../../Engine/SceneRuntime/BlackBoard.h"
#include "../../Engine/SceneRuntime/ScriptReflectionCallbacks.h"
#include "../../Engine/SceneRuntime/ScriptReflectionService.h"
#include <reflgen/json/reader.h>
#include <reflgen/runtime/value_descriptor.h>
#include <cstdio>
#include <map>
#include <stdexcept>
#include <thread>
#include <variant>

namespace reflection_callback_probe
{
    struct Receiver
    {
        ce::owned_func<void()> action;
        ce::owned_func<void(float)> number;
        ce::owned_func<void(math::vector2)> vector;
        ce::owned_func<void(const std::string&)> text;
        ce::owned_func<bool(const BlackBoard&)> condition;
        int assignments{};
        int operand = 9;

        void SetAction(ce::owned_func<void()> callback) { action = callback; ++assignments; }
        void SetFloat(ce::owned_func<void(float)> callback) { number = callback; ++assignments; }
        void SetVector(ce::owned_func<void(math::vector2)> callback) { vector = callback; ++assignments; }
        void SetText(const ce::owned_func<void(const std::string&)>& callback) { text = callback; ++assignments; }
        void SetCondition(ce::owned_func<bool(const BlackBoard&)> callback) { condition = callback; ++assignments; }
        int SetActionAndAdd(ce::owned_func<void()> callback, int& value)
        {
            SetAction(std::move(callback));
            return ++value;
        }
    };
}

template<> struct reflgen::runtime_schema<reflection_callback_probe::Receiver>
{
    using Receiver = reflection_callback_probe::Receiver;
    static constexpr auto value = reflgen::schema<Receiver>(
        reflgen::field<&Receiver::operand>,
        reflgen::method<&Receiver::SetAction>, reflgen::method<&Receiver::SetFloat>,
        reflgen::method<&Receiver::SetVector>, reflgen::method<&Receiver::SetText>,
        reflgen::method<&Receiver::SetCondition>, reflgen::method<&Receiver::SetActionAndAdd>)
        .named("CallbackReceiver");
};

namespace
{
    using ce::script::binding::ScriptApiTable;
    using ce::script::reflection::ResolveCallback;
    using VoidCallback = ce::owned_func<void()>;
    using FloatCallback = ce::owned_func<void(float)>;
    using VectorCallback = ce::owned_func<void(math::vector2)>;
    using TextCallback = ce::owned_func<void(const std::string&)>;
    using ConditionCallback = ce::owned_func<bool(const BlackBoard&)>;

    unsigned checks = 0;

    void Check(bool condition, const char* message)
    {
        ++checks;
        if (!condition)
        {
            throw std::runtime_error(message);
        }
    }

    struct Invocation
    {
        std::uint64_t context{};
        int kind{};
        float x{};
        float y{};
        std::string payload;
    };

    enum class DispatchMode { Record, DisableSelf, EndGeneration, Rebind, Throw };

    struct DispatchState
    {
        ScriptApiTable* table{};
        std::uint64_t generation{};
        std::uint64_t token{};
        DispatchMode mode = DispatchMode::Record;
        std::vector<Invocation> invocations;
        int result = 1;
        int nestedStatus{};
        std::uint64_t nestedRetired{};
        bool nestedResolved{};
        std::function<void()> nestedInvoke;
    } state;

    int __stdcall Dispatch(std::uint64_t context, int kind, float x, float y,
        const char* text, int bytes)
    {
        state.invocations.push_back({context, kind, x, y,
            text && bytes > 0 ? std::string(text, static_cast<std::size_t>(bytes)) : std::string()});
        if (state.mode == DispatchMode::DisableSelf)
        {
            state.nestedStatus = state.table->NativeCallback_Disable(state.token);
            state.nestedRetired = state.table->NativeCallback_TakeRetired();
            state.nestedResolved = static_cast<bool>(ResolveCallback(state.token,
                reflgen::value_descriptor_of<VoidCallback>()));
            if (state.nestedInvoke)
            {
                state.nestedInvoke();
            }
        }
        else if (state.mode == DispatchMode::EndGeneration)
        {
            state.nestedStatus = state.table->NativeCallback_EndGeneration(state.generation);
            state.nestedRetired = state.table->NativeCallback_TakeRetired();
        }
        else if (state.mode == DispatchMode::Rebind)
        {
            try
            {
                ScriptApiTable replacement{};
                ce::script::binding::RegisterReflectionCallbackApi(replacement);
                state.nestedStatus = 1;
            }
            catch (const std::logic_error&)
            {
                state.nestedStatus = -1;
            }
            state.nestedResolved = static_cast<bool>(ResolveCallback(state.token,
                reflgen::value_descriptor_of<VoidCallback>()));
        }
        else if (state.mode == DispatchMode::Throw)
        {
            throw std::runtime_error("callback dispatcher fault injection");
        }
        return state.result;
    }

    template<class Callback> Callback BindCopy(std::uint64_t token)
    {
        // Resolve storage belongs only to this operation. Copying the actual
        // engine function must keep the native callback state alive afterward.
        const auto& expected = reflgen::value_descriptor_of<Callback>();
        auto view = ResolveCallback(token, expected);
        Check(view && view.is_writable() && view.descriptor().id() == expected.id(),
            "callback resolver did not return an exact writable native value");
        return *static_cast<Callback*>(view.mutable_data());
    }

    std::uint64_t Register(int kind, std::uint64_t context)
    {
        const auto token = state.table->NativeCallback_Register(
            state.generation, kind, &Dispatch, context);
        Check(token != 0, "valid typed callback registration failed");
        return token;
    }

    using SnapshotValue = std::variant<std::monostate, bool, std::int64_t, double,
        std::string, std::vector<double>>;
    struct SnapshotEntry
    {
        int type = -1;
        SnapshotValue value;
    };

    std::map<std::string, SnapshotEntry> ReadSnapshot(std::string_view text)
    {
        reflgen::json::reader reader(text);
        std::map<std::string, SnapshotEntry> result;
        reader.begin_object();
        std::string key;
        while (reader.next_key(key))
        {
            SnapshotEntry entry;
            bool seenType = false;
            bool seenValue = false;
            reader.begin_object();
            std::string field;
            while (reader.next_key(field))
            {
                if (field == "type" && !seenType)
                {
                    entry.type = static_cast<int>(reader.read_int());
                    seenType = true;
                }
                else if (field == "value" && !seenValue)
                {
                    seenValue = true;
                    switch (reader.peek())
                    {
                    case reflgen::value_kind::null:
                        reader.read_null();
                        break;
                    case reflgen::value_kind::boolean:
                        entry.value = reader.read_bool();
                        break;
                    case reflgen::value_kind::integer:
                        entry.value = reader.read_int();
                        break;
                    case reflgen::value_kind::floating:
                        entry.value = reader.read_float();
                        break;
                    case reflgen::value_kind::string:
                        entry.value = reader.read_string();
                        break;
                    case reflgen::value_kind::array:
                    {
                        std::vector<double> values;
                        reader.begin_array();
                        while (reader.next_element())
                        {
                            values.push_back(reader.read_float());
                        }
                        reader.end_array();
                        entry.value = std::move(values);
                        break;
                    }
                    default:
                        throw std::runtime_error("unexpected blackboard snapshot payload kind");
                    }
                }
                else
                {
                    throw std::runtime_error("duplicate or unknown blackboard snapshot field");
                }
            }
            reader.end_object();
            Check(seenType && seenValue && result.emplace(key, std::move(entry)).second,
                "blackboard snapshot lost an entry or repeated a key");
        }
        reader.end_object();
        reader.finish();
        return result;
    }

    BlackBoard MakeBlackBoard()
    {
        BlackBoard board;
        // Fixture population through the actual map avoids linking the scene,
        // asset-authoring and entity lookup implementations into this contract
        // executable. The object itself is mutable; only its public accessor
        // returns a const reference. The production snapshot writer is used.
        auto& values = const_cast<ce::engine_node_map<std::string, BlackBoardValue>&>(board.GetValues());
        BlackBoardValue entry{};
        values["none"] = entry;
        entry.Type = BlackBoardType::Bool;
        entry.BoolValue = true;
        values["enabled"] = entry;
        entry.Type = BlackBoardType::Int;
        entry.IntValue = -17;
        values["count"] = entry;
        entry.Type = BlackBoardType::Float;
        entry.FloatValue = 1.25f;
        values["weight"] = entry;
        entry.Type = BlackBoardType::String;
        entry.StringValue = std::string("utf8:\xe9\x9b\xaa\0tail", 13);
        values["text\"\\"] = entry;
        entry.Type = BlackBoardType::Vector2;
        entry.Vec2Value = math::vector2{1.5f, -2.5f};
        values["v2"] = entry;
        entry.Type = BlackBoardType::Vector3;
        entry.Vec3Value = math::vector3{3.0f, 4.0f, -5.0f};
        values["v3"] = entry;
        entry.Type = BlackBoardType::Vector4;
        entry.Vec4Value = math::vector4{6.0f, 7.0f, 8.0f, 9.0f};
        values["v4"] = entry;
        entry.Type = BlackBoardType::Entity;
        entry.StringValue = "entity-name";
        values["target"] = entry;
        entry.Type = BlackBoardType::Transform;
        entry.StringValue = "root/child";
        values["transform"] = entry;
        return board;
    }

    void SignaturesAndSnapshots()
    {
        const auto voidToken = Register(0, 101);
        auto action = BindCopy<VoidCallback>(voidToken);
        action();
        Check(state.invocations.back().context == 101 && state.invocations.back().kind == 0,
            "void callback identity or kind changed");

        auto number = BindCopy<FloatCallback>(Register(1, 102));
        number(-3.25f);
        Check(state.invocations.back().kind == 1 && state.invocations.back().x == -3.25f,
            "float callback argument changed");

        auto vector = BindCopy<VectorCallback>(Register(2, 103));
        vector(math::vector2{1.5f, -2.75f});
        Check(state.invocations.back().kind == 2 && state.invocations.back().x == 1.5f
            && state.invocations.back().y == -2.75f, "vector2 callback arguments changed");

        auto text = BindCopy<TextCallback>(Register(3, 104));
        const std::string payload("A\0\xe9\x9b\xaa", 5);
        text(payload);
        Check(state.invocations.back().kind == 3 && state.invocations.back().payload == payload,
            "string callback did not preserve UTF-8 or embedded zero bytes");

        auto condition = BindCopy<ConditionCallback>(Register(4, 105));
        auto board = MakeBlackBoard();
        state.result = 1;
        Check(condition(board), "condition callback true result was lost");
        Check(state.invocations.back().kind == 4, "blackboard callback kind changed");
        const auto snapshot = ReadSnapshot(state.invocations.back().payload);
        Check(snapshot.size() == 10, "blackboard snapshot is incomplete");
        Check(snapshot.at("none").type == 0 && std::holds_alternative<std::monostate>(snapshot.at("none").value),
            "blackboard None value changed");
        Check(snapshot.at("enabled").type == 1 && std::get<bool>(snapshot.at("enabled").value),
            "blackboard bool value changed");
        Check(snapshot.at("count").type == 2 && std::get<std::int64_t>(snapshot.at("count").value) == -17,
            "blackboard integer value changed");
        Check(snapshot.at("weight").type == 3 && std::get<double>(snapshot.at("weight").value) == 1.25,
            "blackboard float value changed");
        Check(snapshot.at("text\"\\").type == 4
            && std::get<std::string>(snapshot.at("text\"\\").value) == std::string("utf8:\xe9\x9b\xaa\0tail", 13),
            "blackboard text/key escaping or embedded zero bytes changed");
        Check(snapshot.at("v2").type == 5 && std::get<std::vector<double>>(snapshot.at("v2").value) == std::vector<double>{1.5, -2.5},
            "blackboard vector2 value changed");
        Check(snapshot.at("v3").type == 6 && std::get<std::vector<double>>(snapshot.at("v3").value) == std::vector<double>{3, 4, -5},
            "blackboard vector3 value changed");
        Check(snapshot.at("v4").type == 7 && std::get<std::vector<double>>(snapshot.at("v4").value) == std::vector<double>{6, 7, 8, 9},
            "blackboard vector4 value changed");
        Check(snapshot.at("target").type == 8 && std::get<std::string>(snapshot.at("target").value) == "entity-name",
            "blackboard entity authoring identity changed");
        Check(snapshot.at("transform").type == 9 && std::get<std::string>(snapshot.at("transform").value) == "root/child",
            "blackboard transform authoring identity changed");
        state.result = 0;
        Check(!condition(board), "condition callback false result was lost");
        state.result = 1;

        Check(!ResolveCallback(voidToken, reflgen::value_descriptor_of<FloatCallback>()),
            "callback resolver accepted a different callable signature");
        Check(!ResolveCallback(voidToken, reflgen::value_descriptor_of<std::uint64_t>()),
            "callback resolver accepted an ordinary value as a callback");
        Check(state.table->NativeCallback_Register(state.generation, -1, &Dispatch, 1) == 0
            && state.table->NativeCallback_Register(state.generation, 5, &Dispatch, 1) == 0,
            "registration accepted an unknown callback kind");
        Check(state.table->NativeCallback_Register(state.generation, 0, nullptr, 1) == 0,
            "registration accepted a null dispatcher");
        Check(state.table->NativeCallback_Register(state.generation + 1, 0, &Dispatch, 1) == 0,
            "registration accepted another generation");
    }

    void DisableAndThreadAffinity()
    {
        const auto token = Register(0, 201);
        auto action = BindCopy<VoidCallback>(token);
        auto copy = action;
        const auto before = state.invocations.size();
        bool foreignResolved = true;
        std::uint64_t foreignRegistered = 1;
        std::uint64_t foreignGeneration = 1;
        std::uint64_t foreignRetired = 1;
        int foreignDisabled = 0;
        int foreignEnded = 0;
        bool foreignRebindRejected = false;
        std::thread worker([&]
        {
            action();
            foreignResolved = static_cast<bool>(ResolveCallback(token,
                reflgen::value_descriptor_of<VoidCallback>()));
            foreignRegistered = state.table->NativeCallback_Register(state.generation, 0, &Dispatch, 202);
            foreignGeneration = state.table->NativeCallback_BeginGeneration();
            foreignDisabled = state.table->NativeCallback_Disable(token);
            foreignEnded = state.table->NativeCallback_EndGeneration(state.generation);
            foreignRetired = state.table->NativeCallback_TakeRetired();
            try
            {
                ScriptApiTable replacement{};
                ce::script::binding::RegisterReflectionCallbackApi(replacement);
            }
            catch (const std::logic_error&)
            {
                foreignRebindRejected = true;
            }
        });
        worker.join();
        Check(state.invocations.size() == before, "native callback invoked managed code on a worker thread");
        Check(!foreignResolved && foreignRegistered == 0 && foreignGeneration == 0,
            "callback registry accepted worker-thread registration or resolution");
        Check(foreignDisabled == -1 && foreignEnded == -1 && foreignRetired == 0,
            "worker-thread teardown altered the callback generation");
        Check(foreignRebindRejected, "worker-thread session rebind was accepted");
        copy();
        Check(state.invocations.size() == before + 1, "worker rejection disabled the valid owner-thread callback");
        Check(state.table->NativeCallback_Disable(token) == 1, "callback disable failed");
        action();
        copy();
        Check(state.invocations.size() == before + 1, "a copied callback entered managed code after disable");
        Check(!ResolveCallback(token, reflgen::value_descriptor_of<VoidCallback>()),
            "disabled callback remained resolvable");
        Check(state.table->NativeCallback_TakeRetired() == token, "disabled callback did not retire exactly once");
        Check(state.table->NativeCallback_TakeRetired() == 0
            && state.table->NativeCallback_Disable(token) == 0, "callback retirement repeated for a stale token");
    }

    void PayloadFailureBoundaries()
    {
        auto text = BindCopy<TextCallback>(Register(3, 251));
        const std::string boundary(1024 * 1024, 'x');
        text(boundary);
        Check(state.invocations.back().context == 251
            && state.invocations.back().payload == boundary,
            "the largest permitted callback string was rejected or truncated");
        const auto beforeText = state.invocations.size();
        text(boundary + 'x');
        Check(state.invocations.size() == beforeText, "an oversized callback string entered managed code");

        auto condition = BindCopy<ConditionCallback>(Register(4, 252));
        BlackBoard board;
        auto& values = const_cast<ce::engine_node_map<std::string, BlackBoardValue>&>(board.GetValues());
        BlackBoardValue entry{};
        entry.Type = BlackBoardType::String;
        entry.StringValue = boundary;
        values["oversized"] = entry;
        const auto beforeCondition = state.invocations.size();
        Check(!condition(board) && state.invocations.size() == beforeCondition,
            "an oversized blackboard snapshot entered managed code or returned true");
        values.clear();
        entry.Type = BlackBoardType::Float;
        entry.FloatValue = std::numeric_limits<float>::quiet_NaN();
        values["invalid-number"] = entry;
        Check(!condition(board) && state.invocations.size() == beforeCondition,
            "an invalid blackboard number entered managed code or returned true");
        values.clear();
        Check(condition(board) && state.invocations.size() == beforeCondition + 1,
            "snapshot serialization failure disabled the valid callback or leaked its active call");
    }

    void ReentrantDisposal()
    {
        const auto token = Register(0, 301);
        auto action = BindCopy<VoidCallback>(token);
        state.token = token;
        state.mode = DispatchMode::DisableSelf;
        state.nestedInvoke = action;
        const auto before = state.invocations.size();
        action();
        state.nestedInvoke = {};
        state.mode = DispatchMode::Record;
        Check(state.invocations.size() == before + 1, "reentrant disable permitted another managed entry");
        Check(state.nestedStatus == 1 && !state.nestedResolved, "reentrant disable did not immediately revoke admission");
        Check(state.nestedRetired == 0, "managed callback root became releasable before its active call returned");
        Check(state.table->NativeCallback_TakeRetired() == token,
            "reentrant disposal did not publish retirement after the call returned");

        const auto activeToken = Register(0, 302);
        auto active = BindCopy<VoidCallback>(activeToken);
        state.mode = DispatchMode::EndGeneration;
        active();
        state.mode = DispatchMode::Record;
        Check(state.nestedStatus == -1 && state.nestedRetired == 0,
            "reentrant generation unload was accepted while a managed frame was active");
        Check(static_cast<bool>(ResolveCallback(activeToken, reflgen::value_descriptor_of<VoidCallback>())),
            "rejected reentrant unload partially invalidated the current generation");
        const auto stillActive = state.invocations.size();
        active();
        Check(state.invocations.size() == stillActive + 1, "rejected reentrant unload stopped the live callback");

        state.token = activeToken;
        state.mode = DispatchMode::Rebind;
        active();
        state.mode = DispatchMode::Record;
        Check(state.nestedStatus == -1 && state.nestedResolved,
            "reentrant rebind was accepted or invalidated its original callback session");
        const auto afterRejectedRebind = state.invocations.size();
        active();
        Check(state.invocations.size() == afterRejectedRebind + 1,
            "rejected reentrant rebind prevented a later callback from the original session");

        state.mode = DispatchMode::Throw;
        active();
        state.mode = DispatchMode::Record;
        Check(state.table->NativeCallback_Disable(activeToken) == 1
            && state.table->NativeCallback_TakeRetired() == activeToken,
            "dispatcher failure leaked an in-flight callback and blocked retirement");
    }

    void BoundInvocationProtocol()
    {
        namespace protocol = ce::script::reflection;
        using Receiver = reflection_callback_probe::Receiver;
        using Json = protocol::json_value;
        Receiver receiver;
        const auto& descriptor = reflgen::type_descriptor_of<Receiver>();
        protocol::host environment;
        environment.component_types = [&] { return std::vector<const reflgen::type_descriptor*>{&descriptor}; };
        environment.components = [&]
        {
            return std::vector<protocol::component_view>{{&descriptor, &receiver, 17, {}, [] { return true; }}};
        };
        environment.components_for = [&](std::uint32_t index, std::uint32_t generation)
        {
            if (index != 7 || generation != 3)
            {
                throw protocol::protocol_error("stale_owner", "callback fixture owner mismatch");
            }
            return environment.components();
        };
        environment.bind_callback = &ResolveCallback;

        const auto invoke = [&](std::string_view method, Json::array arguments, Json::array bindings)
        {
            const Json request(Json::object{
                {"op", "invoke_bound"}, {"component", "CallbackReceiver"},
                {"instance", std::uint64_t{17}}, {"method", method},
                {"arguments", std::move(arguments)}, {"bindings", std::move(bindings)}});
            return protocol::parse_json(protocol::service::execute(environment, protocol::json_text(request)));
        };
        const auto callback = [](std::uint64_t token)
        {
            return Json(Json::object{{"callback", token}});
        };
        const auto succeeded = [](const Json& response)
        {
            return std::get<bool>(response.require("ok").data);
        };
        const auto failedWith = [&](const Json& response, std::string_view code)
        {
            return !succeeded(response) && response.require("code").as_string() == code;
        };

        const auto actionToken = Register(0, 501);
        const auto floatToken = Register(1, 502);
        const auto vectorToken = Register(2, 503);
        const auto textToken = Register(3, 504);
        const auto conditionToken = Register(4, 505);
        const std::pair<std::string_view, std::uint64_t> setters[] = {
            {"SetAction", actionToken}, {"SetFloat", floatToken}, {"SetVector", vectorToken},
            {"SetText", textToken}, {"SetCondition", conditionToken}};
        for (const auto& [method, token] : setters)
        {
            Check(succeeded(invoke(method, {Json{}}, {callback(token)})),
                "production invoke_bound failed to bind a supported owned callback");
        }
        Check(receiver.assignments == 5, "bound callback assignment executed more or less than once");
        const auto before = state.invocations.size();
        receiver.action();
        receiver.number(7.25f);
        receiver.vector(math::vector2{8.0f, 9.0f});
        receiver.text("retained after service response");
        BlackBoard empty;
        Check(receiver.condition(empty), "bound condition callback lost its return value");
        Check(state.invocations.size() == before + 5
            && state.invocations[before].context == 501
            && state.invocations[before + 1].x == 7.25f
            && state.invocations[before + 2].y == 9.0f
            && state.invocations[before + 3].payload == "retained after service response"
            && state.invocations[before + 4].payload == "{}",
            "callbacks did not retain their real native copies after bound request storage ended");

        Check(failedWith(invoke("SetAction", {Json{}}, {callback(floatToken)}), "invalid_callback"),
            "bound callback signature mismatch was not reported as invalid_callback");
        const Json ambiguous(Json::object{{"callback", actionToken}, {"path", Json::array{}}});
        Check(failedWith(invoke("SetAction", {Json{}}, {ambiguous}), "invalid_request"),
            "a callback binding also containing a path was accepted");
        Check(failedWith(invoke("SetAction", {Json(std::uint64_t{123})}, {callback(actionToken)}), "invalid_request"),
            "callback binding accepted a non-null JSON argument payload");
        Check(failedWith(invoke("SetAction", {Json{}}, {callback(0)}), "invalid_request"),
            "callback binding accepted the zero token");
        Check(failedWith(invoke("SetAction", {Json{}}, {callback(std::numeric_limits<std::uint64_t>::max())}), "invalid_callback"),
            "bound callback invocation accepted an unregistered token");
        Check(receiver.assignments == 5, "invalid callback binding reached the native receiver");

        const Json operandPath(Json::object{
            {"owner", Json::object{{"index", std::uint64_t{7}}, {"generation", std::uint64_t{3}}}},
            {"component", "CallbackReceiver"}, {"instance", std::uint64_t{17}},
            {"path", Json::array{Json::object{{"field", "operand"}}}}});
        Check(succeeded(invoke("SetActionAndAdd", {Json{}, Json{}}, {callback(actionToken), operandPath}))
            && receiver.operand == 10 && receiver.assignments == 6,
            "a callback and a distinct native field argument could not share one bound invocation");
        Check(state.table->NativeCallback_Disable(actionToken) == 1
            && state.table->NativeCallback_TakeRetired() == actionToken,
            "bound callback registration did not retire after disable");
        Check(failedWith(invoke("SetAction", {Json{}}, {callback(actionToken)}), "invalid_callback")
            && receiver.assignments == 6,
            "expired callback token reached the native receiver");
        const auto afterDisable = state.invocations.size();
        receiver.action();
        Check(state.invocations.size() == afterDisable,
            "the receiver's retained function invoked a retired managed registration");
    }

    void GenerationsAndSessions()
    {
        const auto oldGeneration = state.generation;
        const auto oldToken = Register(0, 401);
        auto oldAction = BindCopy<VoidCallback>(oldToken);
        auto oldCondition = BindCopy<ConditionCallback>(Register(4, 402));
        Check(state.table->NativeCallback_EndGeneration(oldGeneration + 1) == 0,
            "mismatched generation invalidated active registrations");
        Check(state.table->NativeCallback_EndGeneration(oldGeneration) == 1,
            "generation shutdown failed with no active managed calls");
        const auto before = state.invocations.size();
        oldAction();
        BlackBoard empty;
        Check(!oldCondition(empty) && state.invocations.size() == before,
            "generation shutdown left callable native copies or a true stale condition");
        Check(!ResolveCallback(oldToken, reflgen::value_descriptor_of<VoidCallback>()),
            "old generation token remained resolvable");
        std::vector<std::uint64_t> retired;
        for (auto token = state.table->NativeCallback_TakeRetired(); token;
             token = state.table->NativeCallback_TakeRetired())
        {
            retired.push_back(token);
        }
        Check(std::count(retired.begin(), retired.end(), oldToken) == 1,
            "generation shutdown did not retire its callback exactly once");
        Check(state.table->NativeCallback_EndGeneration(oldGeneration) == 0,
            "ended callback generation was accepted twice");

        state.generation = state.table->NativeCallback_BeginGeneration();
        Check(state.generation != 0 && state.generation != oldGeneration,
            "callback generation identity was reused");
        Check(state.table->NativeCallback_Register(oldGeneration, 0, &Dispatch, 403) == 0,
            "a previous generation registered a new callback");
        const auto nextToken = Register(0, 404);
        auto nextAction = BindCopy<VoidCallback>(nextToken);
        Check(nextToken != oldToken, "callback token identity was reused");
        ce::script::binding::EndReflectionCallbackSession();
        nextAction();
        oldAction();
        Check(state.invocations.size() == before, "session shutdown left native callback copies active");
        Check(state.table->NativeCallback_Register(state.generation, 0, &Dispatch, 405) == 0
            && state.table->NativeCallback_BeginGeneration() == 0,
            "ended callback session accepted registration");
        Check(!ResolveCallback(nextToken, reflgen::value_descriptor_of<VoidCallback>()),
            "ended callback session resolved a stale token");

        ce::script::binding::RegisterReflectionCallbackApi(*state.table);
        state.generation = state.table->NativeCallback_BeginGeneration();
        Check(state.generation != 0 && state.generation != oldGeneration,
            "rebound callback session reused a generation identity");
        Check(state.table->NativeCallback_TakeRetired() == nextToken
            && state.table->NativeCallback_TakeRetired() == 0,
            "session rebinding lost or duplicated its deferred root-release notification");
        const auto reboundToken = Register(0, 406);
        Check(reboundToken != nextToken && reboundToken != oldToken,
            "rebound callback session reused a callback identity");
        oldAction();
        nextAction();
        Check(state.invocations.size() == before, "session restart resurrected old native callback copies");
        auto rebound = BindCopy<VoidCallback>(reboundToken);
        rebound();
        Check(state.invocations.size() == before + 1 && state.invocations.back().context == 406,
            "new session callback could not execute");
        Check(state.table->NativeCallback_EndGeneration(state.generation) == 1,
            "final callback generation did not drain");
        ce::script::binding::EndReflectionCallbackSession();
    }
}

int main()
{
    try
    {
        ScriptApiTable table{};
        ce::script::binding::RegisterReflectionCallbackApi(table);
        state.table = &table;
        Check(table.NativeCallback_BeginGeneration && table.NativeCallback_Register
            && table.NativeCallback_Disable && table.NativeCallback_TakeRetired
            && table.NativeCallback_EndGeneration, "callback API table slots were not registered");
        state.generation = table.NativeCallback_BeginGeneration();
        Check(state.generation != 0 && table.NativeCallback_BeginGeneration() == 0,
            "callback session did not create one exclusive active generation");
        SignaturesAndSnapshots();
        DisableAndThreadAffinity();
        PayloadFailureBoundaries();
        ReentrantDisposal();
        BoundInvocationProtocol();
        GenerationsAndSessions();
        std::printf("NATIVE_REFLECTION_CALLBACKS_OK checks=%u signatures=5\n", checks);
        return 0;
    }
    catch (const std::exception& error)
    {
        ce::script::binding::EndReflectionCallbackSession();
        std::fprintf(stderr, "NATIVE_REFLECTION_CALLBACKS_FAILED %s\n", error.what());
        return 1;
    }
}
