#pragma once
// The production reflection protocol is independent of Entity/Scene/CLR. A host
// supplies live component roots for one request; no native address is retained
// by the protocol, its paths, or its response tickets.
#include <reflgen/json/reader.h>
#include <reflgen/json/writer.h>
#include <reflgen/runtime/type_descriptor.h>
#include <reflgen/runtime/value_descriptor.h>
#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <functional>
#include <limits>
#include <map>
#include <mutex>
#include <set>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

namespace ce::script::reflection
{
    inline constexpr std::size_t MaximumRequestBytes = 1024 * 1024;
    inline constexpr std::size_t MaximumResponseBytes = 8 * 1024 * 1024;
    inline constexpr std::size_t MaximumPathLength = 64;
    inline constexpr std::size_t MaximumJsonNodes = 131072;

    class protocol_error : public std::runtime_error
    {
    public:
        protocol_error(std::string code, std::string message)
            : std::runtime_error(std::move(message)), code_(std::move(code)) {}
        const std::string& code() const noexcept { return code_; }
    private:
        std::string code_;
    };

    // This small DOM keeps signed and unsigned 64-bit integers distinct. Handles
    // and component identities never travel through double or an address-sized
    // JSON number conversion.
    struct json_value
    {
        using array = std::vector<json_value>;
        using object = std::map<std::string, json_value, std::less<>>;
        using storage = std::variant<std::monostate, bool, std::int64_t,
            std::uint64_t, double, std::string, array, object>;
        storage data;

        json_value() = default;
        json_value(bool value) : data(value) {}
        json_value(std::int64_t value) : data(value) {}
        json_value(std::uint64_t value) : data(value) {}
        json_value(double value) : data(value) {}
        json_value(std::string value) : data(std::move(value)) {}
        json_value(std::string_view value) : data(std::string(value)) {}
        json_value(const char* value) : data(std::string(value)) {}
        json_value(array value) : data(std::move(value)) {}
        json_value(object value) : data(std::move(value)) {}

        const object& as_object() const
        {
            if (const auto* value = std::get_if<object>(&data)) return *value;
            throw protocol_error("invalid_request", "expected a JSON object");
        }
        const array& as_array() const
        {
            if (const auto* value = std::get_if<array>(&data)) return *value;
            throw protocol_error("invalid_request", "expected a JSON array");
        }
        const std::string& as_string() const
        {
            if (const auto* value = std::get_if<std::string>(&data)) return *value;
            throw protocol_error("invalid_request", "expected a JSON string");
        }
        std::uint64_t as_unsigned() const
        {
            if (const auto* value = std::get_if<std::uint64_t>(&data)) return *value;
            if (const auto* value = std::get_if<std::int64_t>(&data); value && *value >= 0)
                return static_cast<std::uint64_t>(*value);
            throw protocol_error("invalid_request", "expected an unsigned 64-bit integer");
        }
        const json_value* find(std::string_view key) const
        {
            const auto& values = as_object();
            const auto found = values.find(key);
            return found == values.end() ? nullptr : &found->second;
        }
        const json_value& require(std::string_view key) const
        {
            if (const auto* value = find(key)) return *value;
            throw protocol_error("invalid_request", "missing request field '" + std::string(key) + "'");
        }
    };

    inline json_value read_json(reflgen::json::reader& in, std::size_t& nodes)
    {
        if (++nodes > MaximumJsonNodes)
            throw protocol_error("limit_exceeded", "JSON value exceeds the node limit");
        switch (in.peek())
        {
        case reflgen::value_kind::null: in.read_null(); return {};
        case reflgen::value_kind::boolean: return json_value(in.read_bool());
        case reflgen::value_kind::integer:
        {
            // A failed probe never changes the actual reader position.
            auto probe = in;
            try
            {
                const auto value = probe.read_int();
                in = std::move(probe);
                return json_value(value);
            }
            catch (const reflgen::serialization_error&) { return json_value(in.read_uint()); }
        }
        case reflgen::value_kind::floating: return json_value(in.read_float());
        case reflgen::value_kind::string: return json_value(in.read_string());
        case reflgen::value_kind::array:
        {
            json_value::array values;
            in.begin_array();
            while (in.next_element()) values.push_back(read_json(in, nodes));
            in.end_array();
            return json_value(std::move(values));
        }
        case reflgen::value_kind::object:
        {
            json_value::object values;
            in.begin_object();
            for (std::string key; in.next_key(key);)
            {
                auto value = read_json(in, nodes);
                if (!values.emplace(key, std::move(value)).second)
                    throw protocol_error("invalid_request", "duplicate JSON property '" + key + "'");
            }
            in.end_object();
            return json_value(std::move(values));
        }
        default: throw protocol_error("invalid_request", "unsupported JSON token");
        }
    }

    inline json_value parse_json(std::string_view text)
    {
        reflgen::json::reader in(text, MaximumPathLength + 16);
        std::size_t nodes = 0;
        auto value = read_json(in, nodes);
        in.finish();
        return value;
    }

    inline void write_json(reflgen::writer& out, const json_value& value)
    {
        std::visit([&out](const auto& item)
        {
            using type = std::decay_t<decltype(item)>;
            if constexpr (std::is_same_v<type, std::monostate>) out.write_null();
            else if constexpr (std::is_same_v<type, bool>) out.write_bool(item);
            else if constexpr (std::is_same_v<type, std::int64_t>) out.write_int(item);
            else if constexpr (std::is_same_v<type, std::uint64_t>) out.write_uint(item);
            else if constexpr (std::is_same_v<type, double>) out.write_float(item);
            else if constexpr (std::is_same_v<type, std::string>) out.write_string(item);
            else if constexpr (std::is_same_v<type, json_value::array>)
            {
                out.begin_array(item.size());
                for (const auto& element : item) write_json(out, element);
                out.end_array();
            }
            else
            {
                out.begin_object(item.size());
                for (const auto& [key, element] : item)
                {
                    out.write_key(key);
                    write_json(out, element);
                }
                out.end_object();
            }
        }, value.data);
    }

    inline std::string json_text(const json_value& value)
    {
        std::string text;
        reflgen::json::writer out(text, 0, MaximumPathLength + 16);
        write_json(out, value);
        return text;
    }

    inline std::string failure(std::string_view code, std::string_view message)
    {
        return json_text(json_value::object{{"ok", false}, {"code", code}, {"message", message}});
    }

    struct component_view
    {
        const reflgen::type_descriptor* type{};
        void* address{};
        std::uint64_t instance{};
        // A production callback captures a temporary GC root and checks that
        // the component remains live before notifying. The callback itself is
        // destroyed when this request completes.
        std::function<void(std::string_view)> notify;
        std::function<bool()> alive;
    };

    struct host
    {
        // Complete Component-derived descriptor catalog, including bases and
        // internal script/placeholder component types. No name-based filter.
        std::function<std::vector<const reflgen::type_descriptor*>()> component_types;
        // Must re-resolve the supplied owner handle and reject stale owners.
        // Returned views only exist for this Execute call.
        std::function<std::vector<component_view>()> components;
    };

    inline std::string_view passing_name(reflgen::parameter_passing passing)
    {
        switch (passing)
        {
        case reflgen::parameter_passing::value: return "value";
        case reflgen::parameter_passing::const_reference: return "const_reference";
        case reflgen::parameter_passing::mutable_reference: return "mutable_reference";
        case reflgen::parameter_passing::rvalue_reference: return "rvalue_reference";
        }
        return "unknown";
    }

    inline std::string method_signature(const reflgen::method_info& method)
    {
        std::string result(method.return_type_name());
        result += ' ';
        result += method.name();
        result += '(';
        bool first = true;
        for (const auto& parameter : method.parameters())
        {
            if (!first) result += ',';
            first = false;
            result += parameter.type_name();
            result += '[';
            result += passing_name(parameter.passing());
            result += ']';
        }
        result += ')';
        if (method.is_const()) result += " const";
        return result;
    }

    inline const reflgen::method_info& find_method(const reflgen::type_descriptor& type,
        std::string_view name, std::string_view signature = {})
    {
        std::map<std::string, std::vector<const reflgen::method_info*>, std::less<>> candidates;
        for (const auto& method : type.methods())
        {
            if (method.name() != name || (!signature.empty() && method_signature(method) != signature)) continue;
            candidates[method_signature(method)].push_back(&method);
        }
        if (candidates.empty())
            throw protocol_error("method_not_found", "method '" + std::string(name) + "' is not reflected");
        if (candidates.size() != 1)
            throw protocol_error("ambiguous_method", "select an exact method signature for '" + std::string(name) + "'");
        if (candidates.begin()->second.size() == 1) return *candidates.begin()->second.front();
        const auto findDeclaring = [](auto&& self, const reflgen::type_descriptor& current,
            reflgen::type_id declaring) -> const reflgen::type_descriptor*
        {
            if (current.id() == declaring) return &current;
            for (const auto& base : current.bases())
                if (const auto* parent = base.descriptor())
                    if (const auto* found = self(self, *parent, declaring)) return found;
            return nullptr;
        };
        const auto& matches = candidates.begin()->second;
        // Identical inherited signatures are an override, not an overload. A
        // candidate is selected only when its declaring class derives from all
        // the other declarations; unrelated multiple bases remain ambiguous.
        for (const auto* candidate : matches)
        {
            const auto* declaring = findDeclaring(findDeclaring, type, candidate->declaring_type());
            if (!declaring) continue;
            const bool mostDerived = std::all_of(matches.begin(), matches.end(), [&](const auto* other)
            {
                return findDeclaring(findDeclaring, *declaring, other->declaring_type()) != nullptr;
            });
            if (mostDerived) return *candidate;
        }
        throw protocol_error("ambiguous_method", "method is inherited from unrelated native bases");
    }

    inline bool readonly_field(const reflgen::field_info& field)
    {
        return field.is_read_only();
    }

    inline std::string_view public_type_name(const reflgen::value_descriptor& value)
    {
        if (const auto* reflected = value.reflected_type()) return reflected->name();
        return value.name();
    }

    inline std::string_view declaring_type_name(const reflgen::type_descriptor& root, reflgen::type_id id)
    {
        if (root.id() == id) return root.name();
        for (const auto& base : root.bases())
            if (const auto* type = base.descriptor())
                if (const auto name = declaring_type_name(*type, id); !name.empty()) return name;
        return {};
    }

    inline json_value describe_type(const reflgen::value_descriptor& value,
        const std::set<std::uint64_t>& componentIds)
    {
        json_value::array fields;
        json_value::array methods;
        json_value::array children;
        if (const auto* type = value.reflected_type())
        {
            for (const auto& field : type->fields())
            {
                fields.emplace_back(json_value::object{
                    {"name", field.name()}, {"serializedName", field.key()}, {"type", public_type_name(field.value())},
                    {"declaringType", declaring_type_name(*type, field.declaring_type())},
                    {"readable", field.value().is_serializable() || field.value().reflected_type() != nullptr},
                    {"writable", !readonly_field(field) && field.value().is_assignable()}});
            }
            for (const auto& method : type->methods())
            {
                json_value::array parameters;
                for (const auto& parameter : method.parameters())
                    parameters.emplace_back(json_value::object{{"name", parameter.name()},
                        {"type", public_type_name(parameter.value())}, {"passing", passing_name(parameter.passing())}});
                methods.emplace_back(json_value::object{{"name", method.name()},
                    {"signature", method_signature(method)}, {"declaringType", declaring_type_name(*type, method.declaring_type())},
                    {"returnType", public_type_name(method.result())}, {"parameters", std::move(parameters)},
                    {"callable", method.is_serialized_invocable()},
                    {"reason", method.serialized_unavailable_reason()},
                    {"canBorrow", method.can_borrow_result()},
                    {"canBorrowWithArguments", method.can_borrow_result_with_arguments()},
                    {"borrowReason", method.borrow_unavailable_reason()}, {"isConst", method.is_const()}});
            }
        }
        for (const auto& child : value.children()) children.emplace_back(public_type_name(child.value()));
        json_value::object result{{"name", public_type_name(value)}, {"id", value.id().value()},
            {"shape", reflgen::value_shape_name(value.shape())},
            {"component", componentIds.contains(value.id().value())},
            {"readable", value.is_serializable() || value.reflected_type() != nullptr},
            {"writable", value.is_assignable()}, {"reason", value.unsupported_reason()},
            {"fields", std::move(fields)}, {"methods", std::move(methods)}, {"children", std::move(children)}};
        return result;
    }

    class service
    {
    public:
        static std::string execute(const host& environment, std::string_view request)
        {
            try
            {
                if (request.empty() || request.size() > MaximumRequestBytes)
                    throw protocol_error("limit_exceeded", "request size must be between 1 byte and 1 MiB");
                const auto parsed = parse_json(request);
                auto value = perform(environment, parsed);
                auto response = json_text(json_value::object{{"ok", true}, {"value", std::move(value)}});
                if (response.size() > MaximumResponseBytes)
                    throw protocol_error("limit_exceeded", "response exceeds 8 MiB; request a narrower member path");
                return response;
            }
            catch (const protocol_error& error) { return failure(error.code(), error.what()); }
            catch (const std::out_of_range& error) { return failure("out_of_range", error.what()); }
            catch (const reflgen::serialization_error& error) { return failure("invalid_value", error.what()); }
            catch (const std::invalid_argument& error) { return failure("unsupported", error.what()); }
            catch (const std::exception& error) { return failure("native_error", error.what()); }
            catch (...) { return failure("native_error", "unknown native exception"); }
        }

    private:
        static json_value perform(const host& environment, const json_value& request);
        static reflgen::value_view resolve_path(reflgen::value_view current, const json_value::array& path,
            std::string& rootField, const std::function<bool()>& alive);
        static json_value read_value(reflgen::value_view value, std::size_t depth,
            std::set<std::pair<std::uint64_t, const void*>>& visiting, std::size_t& nodes);
        static void write_value(reflgen::value_view value, const json_value& replacement);
    };

    inline reflgen::value_view service::resolve_path(reflgen::value_view current,
        const json_value::array& path, std::string& rootField, const std::function<bool()>& alive)
    {
        if (path.size() > MaximumPathLength)
            throw protocol_error("limit_exceeded", "member path exceeds 64 segments");
        for (const auto& segment : path)
        {
            if (alive && !alive()) throw protocol_error("stale_component", "the component was removed while resolving its member path");
            const auto& fields = segment.as_object();
            const auto* member = segment.find("field");
            const auto* index = segment.find("index");
            const auto* key = segment.find("key");
            const auto* dereference = segment.find("deref");
            const auto* methodName = segment.find("method");
            const auto* signature = segment.find("signature");
            const auto* arguments = segment.find("arguments");
            if ((member != nullptr) + (index != nullptr) + (key != nullptr) + (dereference != nullptr)
                + (methodName != nullptr) != 1 || fields.size() != 1u + (signature ? 1u : 0u) + (arguments ? 1u : 0u)
                || ((signature || arguments) && !methodName))
                throw protocol_error("invalid_request", "a path segment must select exactly one field, index, key, deref or method");
            if (!current) throw protocol_error("null_value", "cannot traverse a null native value");
            if (member)
            {
                if (rootField.empty()) rootField = member->as_string();
                current = current.select_member(member->as_string());
            }
            else if (index)
            {
                const auto ordinal = index->as_unsigned();
                if (ordinal > (std::numeric_limits<std::size_t>::max)())
                    throw protocol_error("out_of_range", "container index exceeds the native size range");
                current = current.select_index(static_cast<std::size_t>(ordinal));
            }
            else if (key)
            {
                if (!current.descriptor().can_select_key())
                    throw protocol_error("unsupported", "this value does not expose map keys");
                const auto& wanted = key->as_string();
                reflgen::value_view found;
                for (std::size_t ordinal = 0; ordinal < current.size(); ++ordinal)
                {
                    std::set<std::pair<std::uint64_t, const void*>> visiting;
                    std::size_t nodes = 0;
                    const auto actual = read_value(current.select_key(ordinal), 0, visiting, nodes);
                    const auto* text = std::get_if<std::string>(&actual.data);
                    if ((text && *text == wanted) || (!text && json_text(actual) == wanted))
                    {
                        if (found) throw protocol_error("ambiguous_key", "map contains multiple matching keys");
                        found = current.select_index(ordinal);
                    }
                }
                if (!found) throw protocol_error("key_not_found", "map key '" + wanted + "' was not found");
                current = found;
            }
            else if (dereference)
            {
                const auto* enabled = std::get_if<bool>(&dereference->data);
                if (!enabled || !*enabled)
                    throw protocol_error("invalid_request", "deref must be true");
                current = current.dereference();
            }
            else
            {
                const auto* type = current.descriptor().reflected_type();
                if (!type) throw protocol_error("unsupported", "this value does not expose methods");
                const auto& method = find_method(*type, methodName->as_string(),
                    signature ? signature->as_string() : std::string_view{});
                if (!method.can_borrow_result())
                    throw protocol_error("unsupported", std::string(method.borrow_unavailable_reason()));
                if (!current.is_writable() && !method.is_const())
                    throw protocol_error("read_only", "cannot call a mutable getter through a read-only path");
                if (rootField.empty()) rootField = "*";
                reflgen::value_view next;
                if (arguments)
                {
                    const auto argumentText = json_text(*arguments);
                    reflgen::json::reader in(argumentText, MaximumPathLength + 16);
                    next = current.is_writable() ? method.borrow_result(current.mutable_data(), in)
                        : method.borrow_result(current.data(), in);
                    in.finish();
                }
                else
                {
                    if (!method.parameters().empty())
                        throw protocol_error("invalid_request", "this native lookup requires arguments");
                    next = current.is_writable() ? method.borrow_result(current.mutable_data())
                        : method.borrow_result(current.data());
                }
                next.retain_parent(current);
                current = std::move(next);
            }
        }
        if (!current) throw protocol_error("null_value", "native path resolves to null");
        return current;
    }

    inline json_value service::read_value(reflgen::value_view value, std::size_t depth,
        std::set<std::pair<std::uint64_t, const void*>>& visiting, std::size_t& nodes)
    {
        if (++nodes > MaximumJsonNodes || depth > MaximumPathLength)
            throw protocol_error("limit_exceeded", "value is too large or deeply nested; read a narrower member path");
        if (!value) return {};
        const auto identity = std::make_pair(value.descriptor().id().value(), value.data());
        if (!visiting.insert(identity).second)
            throw protocol_error("cyclic_value", "value graph contains a cycle; read a narrower member path");
        struct visit_guard
        {
            std::set<std::pair<std::uint64_t, const void*>>& set;
            std::pair<std::uint64_t, const void*> identity;
            ~visit_guard() { set.erase(identity); }
        } guard{visiting, identity};

        const auto& descriptor = value.descriptor();
        if (const auto* record = descriptor.reflected_type(); record && descriptor.shape() == reflgen::value_shape::record)
        {
            json_value::object result;
            for (const auto& field : record->fields())
                if (!result.emplace(std::string(field.key()),
                    read_value(value.select_member(field.name()), depth + 1, visiting, nodes)).second)
                    throw protocol_error("ambiguous_member", "native record has duplicate serialized field keys");
            return result;
        }
        if (descriptor.shape() == reflgen::value_shape::pointer || descriptor.shape() == reflgen::value_shape::optional)
        {
            if (!descriptor.can_dereference())
                throw protocol_error("unsupported", "native pointer type '" + std::string(descriptor.name())
                    + "' does not permit a borrowed value view");
            if (value.size() == 0) return {};
            return read_value(value.dereference(), depth + 1, visiting, nodes);
        }
        if (descriptor.shape() == reflgen::value_shape::map && descriptor.can_select_key())
        {
            json_value::object object;
            json_value::array pairs;
            bool objectKeys = true;
            for (std::size_t index = 0; index < value.size(); ++index)
            {
                auto key = read_value(value.select_key(index), depth + 1, visiting, nodes);
                auto mapped = read_value(value.select_index(index), depth + 1, visiting, nodes);
                const auto* text = std::get_if<std::string>(&key.data);
                const bool scalar = text || std::holds_alternative<std::int64_t>(key.data)
                    || std::holds_alternative<std::uint64_t>(key.data) || std::holds_alternative<bool>(key.data);
                if (!scalar || !object.emplace(text ? *text : json_text(key), mapped).second) objectKeys = false;
                pairs.emplace_back(json_value::array{std::move(key), std::move(mapped)});
            }
            return objectKeys ? json_value(std::move(object)) : json_value(std::move(pairs));
        }
        // Variants and expected results have an active-branch discriminator in
        // their runtime wire format. Flattening them to a one-element array
        // would lose the branch and could not be passed back to a native method.
        if (descriptor.can_select_index() && descriptor.shape() != reflgen::value_shape::variant
            && descriptor.shape() != reflgen::value_shape::expected)
        {
            json_value::array result;
            const auto size = value.size();
            if (size > MaximumJsonNodes - nodes)
                throw protocol_error("limit_exceeded", "container exceeds the value node limit");
            result.reserve(size);
            for (std::size_t index = 0; index < size; ++index)
                result.push_back(read_value(value.select_index(index), depth + 1, visiting, nodes));
            return result;
        }
        if (!descriptor.is_serializable())
            throw protocol_error("unsupported", "type '" + std::string(descriptor.name()) + "' has no value serializer");
        std::string text;
        reflgen::json::writer out(text);
        value.serialize(out);
        return parse_json(text);
    }

    inline void service::write_value(reflgen::value_view value, const json_value& replacement)
    {
        if (!value.is_writable()) throw protocol_error("read_only", "native member path is read-only");
        if (!value.descriptor().is_assignable())
            throw protocol_error("unsupported", "type '" + std::string(value.descriptor().name())
                + "' cannot be safely replaced; select a writable child member");
        // A whole record replacement must not bypass child readonly metadata.
        const auto check = [](auto&& self, const reflgen::value_descriptor& descriptor,
            const json_value& proposed, std::size_t depth) -> void
        {
            if (depth > MaximumPathLength) throw protocol_error("limit_exceeded", "replacement value is too deeply nested");
            if (const auto* record = descriptor.reflected_type(); record && descriptor.shape() == reflgen::value_shape::record)
            {
                for (const auto& [name, child] : proposed.as_object())
                {
                    const reflgen::field_info* field = nullptr;
                    for (const auto& candidate : record->fields())
                        if (candidate.key() == name)
                        {
                            if (field) throw protocol_error("ambiguous_member", "native record has duplicate serialized field keys");
                            field = &candidate;
                        }
                    if (!field) throw protocol_error("member_not_found", "serialized field '" + name + "' is not reflected");
                    if (readonly_field(*field))
                        throw protocol_error("read_only", "field '" + name + "' is read-only");
                    self(self, field->value(), child, depth + 1);
                }
            }
            else if (descriptor.shape() == reflgen::value_shape::sequence && descriptor.children().size() == 1)
            {
                for (const auto& child : proposed.as_array())
                    self(self, descriptor.children().front().value(), child, depth + 1);
            }
        };
        check(check, value.descriptor(), replacement, 0);
        const auto text = json_text(replacement);
        reflgen::json::reader in(text, MaximumPathLength + 16);
        value.deserialize(in); // Stages a value and commits only after successful deserialization.
        in.finish();
    }

    inline json_value service::perform(const host& environment, const json_value& request)
    {
        const auto& operation = request.require("op").as_string();
        const auto roots = environment.component_types();
        std::set<std::uint64_t> componentIds;
        for (const auto* type : roots) componentIds.insert(type->id().value());

        if (operation == "catalog")
        {
            json_value::array result;
            for (const auto* value : reflgen::value_type_closure(roots))
                result.push_back(describe_type(*value, componentIds));
            return result;
        }
        if (operation == "describe" && !request.find("component"))
        {
            const auto& wanted = request.require("type").as_string();
            for (const auto* value : reflgen::value_type_closure(roots))
                if (public_type_name(*value) == wanted) return describe_type(*value, componentIds);
            throw protocol_error("type_not_found", "type '" + wanted + "' is not in the native component value closure");
        }

        const auto components = environment.components();
        if (operation == "components")
        {
            json_value::array result;
            for (const auto& component : components)
            {
                if (!component.type || !component.address || (component.alive && !component.alive())) continue;
                result.emplace_back(json_value::object{{"type", component.type->name()},
                    {"typeId", component.type->id().value()}, {"instance", component.instance}});
            }
            return result;
        }
        if (operation != "describe" && operation != "get" && operation != "set" && operation != "invoke")
            throw protocol_error("invalid_operation", "unknown reflection operation '" + operation + "'");

        const auto& wanted = request.require("component").as_string();
        const auto instance = request.require("instance").as_unsigned();
        const component_view* root = nullptr;
        for (const auto& candidate : components)
        {
            if (candidate.type && candidate.type->name() == wanted && candidate.instance == instance
                && candidate.address && (!candidate.alive || candidate.alive()))
            {
                root = &candidate;
                break;
            }
        }
        if (!root) throw protocol_error("stale_component", "the selected component instance no longer exists on this owner");

        static const json_value::array emptyPath;
        const auto* pathValue = request.find("path");
        const auto& path = pathValue ? pathValue->as_array() : emptyPath;
        std::string rootField;
        auto selected = resolve_path(root->type->value().view(root->address), path, rootField, root->alive);
        if (root->alive && !root->alive())
            throw protocol_error("stale_component", "the component was removed while resolving its member path");
        if (operation == "describe")
        {
            auto description = describe_type(selected.descriptor(), componentIds);
            auto& fields = std::get<json_value::object>(description.data);
            fields.insert_or_assign("writable", selected.is_writable() && selected.descriptor().is_assignable());
            fields.emplace("mutable", selected.is_writable());
            if (selected.descriptor().can_select_index())
                fields.emplace("count", static_cast<std::uint64_t>(selected.size()));
            return description;
        }
        if (operation == "get")
        {
            std::set<std::pair<std::uint64_t, const void*>> visiting;
            std::size_t nodes = 0;
            return read_value(selected, 0, visiting, nodes);
        }
        if (operation == "set")
        {
            if (path.empty()) throw protocol_error("read_only", "a native component cannot be replaced; select a member path");
            write_value(selected, request.require("value"));
            if (root->notify) root->notify(rootField.empty() ? std::string_view("*") : std::string_view(rootField));
            return {};
        }

        const auto* type = selected.descriptor().reflected_type();
        if (!type) throw protocol_error("unsupported", "the selected value has no reflected methods");
        const auto* signature = request.find("signature");
        const auto& method = find_method(*type, request.require("method").as_string(),
            signature ? signature->as_string() : std::string_view{});
        if (!selected.is_writable() && !method.is_const())
            throw protocol_error("read_only", "cannot invoke a mutable method through a read-only path");
        if (!method.is_serialized_invocable())
            throw protocol_error("unsupported", std::string(method.serialized_unavailable_reason()));
        const auto* arguments = request.find("arguments");
        const auto argumentText = json_text(arguments ? *arguments : json_value(json_value::array{}));
        reflgen::json::reader in(argumentText, MaximumPathLength + 16);
        std::string result;
        reflgen::json::writer out(result);
        method.invoke_serialized(const_cast<void*>(selected.data()), in, out);
        in.finish();
        if (!method.is_const() && root->notify)
            root->notify(rootField.empty() ? std::string_view("*") : std::string_view(rootField));
        return parse_json(result);
    }

    // Bounded response storage separates execution from the managed buffer-size
    // handshake. Reading or resizing a response buffer never repeats a method.
    class response_tickets
    {
    public:
        static constexpr std::size_t MaximumTickets = 64;
        static constexpr std::size_t MaximumTotalBytes = 16 * 1024 * 1024;

        template<class Execute>
        std::uint64_t execute(Execute&& operation) noexcept
        {
            std::uint64_t ticket = 0;
            try
            {
                {
                    std::lock_guard lock(mutex_);
                    if (responses_.size() >= MaximumTickets || reservedBytes_ > MaximumTotalBytes - MaximumResponseBytes
                        || next_ == (std::numeric_limits<std::uint64_t>::max)()) return 0;
                    ticket = next_++;
                    responses_.emplace(ticket, std::string{});
                    reservedBytes_ += MaximumResponseBytes;
                }
                std::string response;
                try { response = operation(); }
                catch (const std::exception& error) { response = failure("native_error", error.what()); }
                catch (...) { response = failure("native_error", "unknown native exception"); }
                if (response.size() > MaximumResponseBytes)
                    response = failure("limit_exceeded", "response exceeds 8 MiB");
                std::lock_guard lock(mutex_);
                auto found = responses_.find(ticket);
                if (found == responses_.end()) return 0;
                reservedBytes_ -= MaximumResponseBytes;
                reservedBytes_ += response.size();
                found->second = std::move(response);
                return ticket;
            }
            catch (...)
            {
                if (ticket != 0) release(ticket);
                return 0;
            }
        }

        int read(std::uint64_t ticket, char* output, int capacity) const noexcept
        {
            try
            {
                if (capacity < 0 || (capacity > 0 && !output)) return -1;
                std::lock_guard lock(mutex_);
                const auto found = responses_.find(ticket);
                if (found == responses_.end() || found->second.empty()) return -1;
                const int needed = static_cast<int>(found->second.size());
                if (capacity >= needed) std::memcpy(output, found->second.data(), found->second.size());
                return needed;
            }
            catch (...) { return -1; }
        }

        void release(std::uint64_t ticket) noexcept
        {
            try
            {
                std::lock_guard lock(mutex_);
                const auto found = responses_.find(ticket);
                if (found != responses_.end())
                {
                    reservedBytes_ -= found->second.empty() ? MaximumResponseBytes : found->second.size();
                    responses_.erase(found);
                }
            }
            catch (...) {}
        }

        void clear() noexcept
        {
            try
            {
                std::lock_guard lock(mutex_);
                responses_.clear();
                reservedBytes_ = 0;
                // IDs deliberately never restart: tickets from an earlier play
                // session cannot alias a later response.
            }
            catch (...) {}
        }

    private:
        mutable std::mutex mutex_;
        std::map<std::uint64_t, std::string> responses_;
        std::uint64_t next_ = 1;
        std::size_t reservedBytes_ = 0;
    };
}
