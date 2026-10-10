#include "InputAccessorGenerator.h"

#include <sstream>
#include <unordered_set>

namespace Input
{
    namespace
    {
        bool IsIdentifier(std::string_view name)
        {
            if (name.empty() || name.front() == '_' || (name.front() >= '0' && name.front() <= '9'))
            {
                return false;
            }
            for (const auto ch : name)
            {
                if (!((ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') ||
                    (ch >= '0' && ch <= '9') || ch == '_'))
                {
                    return false;
                }
            }
            // Reject C++ keywords; C# identifiers additionally use verbatim @ syntax.
            static const std::unordered_set<std::string_view> keywords{
                "alignas", "alignof", "and", "and_eq", "asm", "auto", "bitand", "bitor", "bool", "break",
                "case", "catch", "char", "char8_t", "char16_t", "char32_t", "class", "compl", "concept",
                "const", "consteval", "constexpr", "constinit", "const_cast", "continue", "co_await", "co_return",
                "co_yield", "decltype", "default", "delete", "do", "double", "dynamic_cast", "else", "enum",
                "explicit", "export", "extern", "false", "float", "for", "friend", "goto", "if", "inline",
                "int", "long", "mutable", "namespace", "new", "noexcept", "not", "not_eq", "nullptr",
                "operator", "or", "or_eq", "private", "protected", "public", "register", "reinterpret_cast",
                "requires", "return", "short", "signed", "sizeof", "static", "static_assert", "static_cast",
                "struct", "switch", "template", "this", "thread_local", "throw", "true", "try", "typedef",
                "typeid", "typename", "union", "unsigned", "using", "virtual", "void", "volatile", "wchar_t",
                "while", "xor", "xor_eq" };
            return !keywords.contains(name);
        }
    }

    bool GenerateInputAccessors(const InputGraphProgram& program, std::string_view className,
        InputAccessorSources& sources, std::string& diagnostic)
    {
        sources = {};
        diagnostic.clear();
        if (!IsIdentifier(className) || className == "Layers")
        {
            diagnostic = "Accessor class name must be a non-reserved ASCII code identifier.";
            return false;
        }
        const auto& graph = program.GetDefinition();
        std::unordered_set<std::string> names{ std::string(className), "Layers" };
        for (const auto& signal : graph.signals)
        {
            if (!IsIdentifier(signal.name) || !names.insert(signal.name).second)
            {
                diagnostic = "Signal accessor name is invalid or collides: " + signal.name;
                return false;
            }
        }

        std::unordered_set<std::string> layerNames{ "Layers" };
        for (const auto& layer : graph.layers)
        {
            if (!IsIdentifier(layer.name) || !layerNames.insert(layer.name).second)
            {
                diagnostic = "Layer accessor name is invalid or collides: " + layer.name;
                return false;
            }
        }

        std::ostringstream cpp;
        std::ostringstream csharp;
        cpp << "// Generated from compiled LX InputGraph. Do not edit.\n#pragma once\n"
            << "#include \"InputGraphTypes.h\"\n\nnamespace Generated\n{\n    struct " << className << " final\n    {\n";
        csharp << "// Generated from compiled LX InputGraph. Do not edit.\nnamespace CreatorEngine.Generated\n{\n"
            << "    public static class @" << className << "\n    {\n";
        for (const auto& signal : graph.signals)
        {
            const char* cppType = nullptr;
            const char* managedType = nullptr;
            const char* factory = nullptr;
            switch (signal.type)
            {
            case ValueType::Button:
                cppType = "::Input::Button";
                managedType = "bool";
                factory = "Button";
                break;
            case ValueType::Float:
                cppType = "float";
                managedType = "float";
                factory = "Float";
                break;
            case ValueType::Vector2:
                cppType = "::Input::InputVector2";
                managedType = "global::CreatorEngine.Float2";
                factory = "Vector2";
                break;
            default:
                diagnostic = "Unknown signal type cannot be exported.";
                return false;
            }
            cpp << "        static constexpr ::Input::InputSignal<" << cppType << "> " << signal.name << "{ { "
                << graph.id.high << "ULL, " << graph.id.low << "ULL }, { " << signal.id.high << "ULL, "
                << signal.id.low << "ULL }, " << program.GetInterfaceHash() << "ULL, " << graph.schemaVersion
                << "u, " << graph.abiVersion << "u };\n";
            csharp << "        public static readonly global::CreatorEngine.InputSignal<" << managedType << "> @" << signal.name
                << " = global::CreatorEngine.InputSignal." << factory << "(new global::CreatorEngine.InputID(" << graph.id.high << "UL, " << graph.id.low
                << "UL), new global::CreatorEngine.InputID(" << signal.id.high << "UL, " << signal.id.low << "UL), "
                << program.GetInterfaceHash() << "UL, " << graph.schemaVersion << "u, " << graph.abiVersion << "u);\n";
        }
        cpp << "        struct Layers final\n        {\n";
        csharp << "        public static class Layers\n        {\n";
        for (const auto& layer : graph.layers)
        {
            cpp << "            static constexpr ::Input::LayerID " << layer.name << "{ "
                << layer.id.high << "ULL, " << layer.id.low << "ULL };\n";
            csharp << "            public static readonly global::CreatorEngine.InputID @" << layer.name
                << " = new(" << layer.id.high << "UL, " << layer.id.low << "UL);\n";
        }
        cpp << "        };\n    };\n}\n";
        csharp << "        }\n    }\n}\n";
        sources.cpp = cpp.str();
        sources.csharp = csharp.str();
        return true;
    }
}
