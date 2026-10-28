#pragma once

#include "../../Engine/Containers/ContainerAPI.h"
#include <reflgen/runtime/type_descriptor.h>

namespace Meta
{
    enum class MethodInputKind { Int, Float, Bool, String, Utf8String, Utf16String, Unsupported };

    inline MethodInputKind MethodInputOf(reflgen::type_id type)
    {
        if (type == reflgen::type_id_of<int>()) return MethodInputKind::Int;
        if (type == reflgen::type_id_of<float>()) return MethodInputKind::Float;
        if (type == reflgen::type_id_of<bool>()) return MethodInputKind::Bool;
        if (type == reflgen::type_id_of<std::string>()) return MethodInputKind::String;
        if (type == reflgen::type_id_of<ce::utf8_string>()) return MethodInputKind::Utf8String;
        if (type == reflgen::type_id_of<ce::string>()) return MethodInputKind::Utf16String;
        return MethodInputKind::Unsupported;
    }

    // method_info::invoke consumes addresses of the actual parameter value
    // types, with references/cv removed. Never pass a converted temporary here.
    inline void* MethodArgumentAddress(std::any& argument)
    {
        if (auto* value = std::any_cast<int>(&argument)) return value;
        if (auto* value = std::any_cast<float>(&argument)) return value;
        if (auto* value = std::any_cast<bool>(&argument)) return value;
        if (auto* value = std::any_cast<std::string>(&argument)) return value;
        if (auto* value = std::any_cast<ce::utf8_string>(&argument)) return value;
        if (auto* value = std::any_cast<ce::string>(&argument)) return value;
        return nullptr;
    }

    inline std::string InvokeAndDescribe(void* instance, const reflgen::method_info& method,
        std::span<void* const> arguments)
    {
        switch (MethodInputOf(method.return_type()))
        {
        case MethodInputKind::Int: { int result{}; method.invoke(instance, arguments, &result); return "Result: " + std::to_string(result); }
        case MethodInputKind::Float: { float result{}; method.invoke(instance, arguments, &result); return "Result: " + std::to_string(result); }
        case MethodInputKind::Bool: { bool result{}; method.invoke(instance, arguments, &result); return result ? "Result: true" : "Result: false"; }
        case MethodInputKind::String: { std::string result; method.invoke(instance, arguments, &result); return "Result: " + result; }
        case MethodInputKind::Utf8String:
        {
            ce::utf8_string result;
            method.invoke(instance, arguments, &result);
            return "Result: " + std::string(result.data(), result.size());
        }
        case MethodInputKind::Utf16String:
        {
            ce::string result;
            method.invoke(instance, arguments, &result);
            ce::utf8_string bytes;
            if (!ce::to_utf8(result.view(), bytes))
                throw std::invalid_argument("method returned invalid UTF-16 text");
            return "Result: " + std::string(bytes.data(), bytes.size());
        }
        case MethodInputKind::Unsupported: break;
        }
        method.invoke(instance, arguments);
        return method.return_type() == reflgen::type_id_of<void>() ? "Invoked" : "Invoked (result cannot be displayed)";
    }
}
