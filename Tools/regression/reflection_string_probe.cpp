#include "../../Engine/Utility_Framework/Reflection/ReflgenStringSerializers.h"
#include "../../Engine/Utility_Framework/Reflection/ReflgenAuthoring.h"
#include "../../Engine/Utility_Framework/Reflection/ReflectionContainer.h"
#include "../../Engine/Utility_Framework/Foundation/EngineNodeMap.h"
#include "../../Engine/Utility_Framework/Foundation/EngineOrderedMap.h"
#include "../../Editor/ImGuiHelper/ReflectedMethodText.h"
#include <reflgen/json.h>
#include <cstdio>
#include <stdexcept>

namespace
{
    unsigned checks = 0;
    void Check(bool success, const char* message)
    {
        ++checks;
        if (!success) throw std::runtime_error(message);
    }

    struct Strings
    {
        ce::string title;
        ce::utf8_string identifier;
        ce::dynamic_array<ce::string> names;
        ce::engine_ordered_map<ce::utf8_string, ce::string> labels;
        std::string legacy;

        static consteval auto reflect()
        {
            return reflgen::schema<Strings>(
                reflgen::field<&Strings::title>, reflgen::field<&Strings::identifier>,
                reflgen::field<&Strings::names>, reflgen::field<&Strings::labels>,
                reflgen::field<&Strings::legacy>);
        }
    };

    struct TaggedAllocator : ce::heap_allocator
    {
        int tag = 0;
        bool operator==(const TaggedAllocator& other) const noexcept { return tag == other.tag; }
    };

    struct Methods
    {
        ce::string Utf16(const ce::string& input) { return input; }
        ce::utf8_string Utf8(ce::utf8_string input) { return input; }
        std::string Legacy(const std::string& input) { return input; }
        ce::string Invalid()
        {
            const char16_t invalid = 0xd800;
            return ce::string(&invalid, 1);
        }
        static consteval auto reflect()
        {
            return reflgen::schema<Methods>(reflgen::method<&Methods::Utf16>,
                reflgen::method<&Methods::Utf8>, reflgen::method<&Methods::Legacy>,
                reflgen::method<&Methods::Invalid>);
        }
    };

    template<class T>
    std::string Yaml(const T& value)
    {
        Authoring::WriteDocument document;
        Authoring::ReflgenWriter writer(document.Root());
        reflgen::serialize(writer, value);
        return document.Dump();
    }

    template<class T>
    T ReadYaml(std::string_view text)
    {
        auto document = ryml::parse_in_arena(ryml::csubstr(text.data(), text.size()));
        const auto root = Authoring::ReadNode::FromRyml(document);
        Check(bool(root), "serialized YAML could not be parsed");
        Authoring::ReflgenReader reader(root);
        return reflgen::deserialize<T>(reader);
    }

    void Scalars()
    {
        const ce::string utf16(u"한글 \U0001F680 \"quoted\"\\line\nnext");
        ce::utf8_string utf8;
        Check(bool(ce::to_utf8(utf16.view(), utf8)), "test Unicode conversion failed");
        const std::string legacy(utf8.data(), utf8.size());
        Check(reflgen::json::to_string(utf16) == reflgen::json::to_string(legacy),
            "UTF16 JSON format differs from legacy UTF8 text");
        Check(reflgen::json::to_string(utf8) == reflgen::json::to_string(legacy),
            "engine UTF8 JSON format differs from std string");
        Check(reflgen::json::from_string<ce::string>(reflgen::json::to_string(utf16)) == utf16,
            "UTF16 JSON roundtrip lost text");
        Check(reflgen::json::from_string<ce::utf8_string>(reflgen::json::to_string(utf8)) == utf8,
            "UTF8 JSON roundtrip lost text");
        Check(Yaml(utf16) == Yaml(legacy) && Yaml(utf8) == Yaml(legacy),
            "engine YAML string representation differs from the standard boundary");
        Check(ReadYaml<ce::string>(Yaml(utf16)) == utf16, "UTF16 YAML roundtrip lost text");
        Check(ReadYaml<ce::utf8_string>(Yaml(utf8)) == utf8, "UTF8 YAML roundtrip lost text");

        const char16_t nul[] = {u'a', u'\0', u'한', 0xd83d, 0xde80};
        const ce::string withNul(nul, 5);
        Check(reflgen::json::from_string<ce::string>(reflgen::json::to_string(withNul)) == withNul,
            "JSON lost an embedded NUL or surrogate pair");
        Check(ReadYaml<ce::string>(Yaml(withNul)) == withNul, "YAML lost embedded NUL text");
        Check(ReadYaml<ce::utf8_string>(Yaml(ce::utf8_string{})).empty(), "empty UTF8 became null");
        Check(ReadYaml<ce::string>(Yaml(ce::string{})).empty(), "empty UTF16 became null");

        for (const char* reserved : {"~", "null", "Null", "NULL", "true", "12", "1.5"})
        {
            const ce::utf8_string value(reserved);
            Check(ReadYaml<ce::utf8_string>(Yaml(value)) == value, "typed string was reinterpreted as YAML scalar");
            Check(ReadYaml<std::string>(Yaml(std::string(reserved))) == reserved,
                "legacy typed string boundary lost reserved text");
        }

        TaggedAllocator allocator;
        allocator.tag = 73;
        ce::basic_utf16_string<TaggedAllocator> custom(allocator);
        reflgen::json::from_string(reflgen::json::to_string(utf16), custom);
        Check(custom.view() == utf16.view() && custom.get_allocator().tag == 73,
            "UTF16 deserialization changed allocator ownership");
        ce::basic_utf8_string<TaggedAllocator> custom8(allocator);
        reflgen::json::from_string(reflgen::json::to_string(utf8), custom8);
        Check(custom8.view() == utf8.view() && custom8.get_allocator().tag == 73,
            "UTF8 deserialization changed allocator ownership");
    }

    void ContainersAndRecords()
    {
        Strings source;
        source.title.assign(u"장면 \U0001F680");
        source.identifier.assign("scene/main");
        source.names.emplace_back(u"첫째");
        source.names.emplace_back(u"둘째 \U0001F600");
        source.labels.emplace(ce::utf8_string("한국어"), ce::string(u"한글"));
        source.labels.emplace(ce::utf8_string("null"), ce::string(u"~"));
        source.legacy = "std ABI boundary";
        const auto json = reflgen::json::to_string(source);
        const auto jsonRoundtrip = reflgen::json::from_string<Strings>(json);
        Check(jsonRoundtrip.title == source.title && jsonRoundtrip.identifier == source.identifier,
            "reflected engine scalar fields were not serialized");
        Check(jsonRoundtrip.names == source.names && jsonRoundtrip.legacy == source.legacy,
            "reflected sequence or existing standard field changed");
        Check(jsonRoundtrip.labels.at(ce::utf8_string("한국어")) == ce::string(u"한글"),
            "reflected UTF8 keyed map lost Unicode");
        Check(json.find("\"labels\":{") != std::string::npos, "string-keyed map changed into a pair array");
        const auto yamlRoundtrip = ReadYaml<Strings>(Yaml(source));
        Check(yamlRoundtrip.title == source.title && yamlRoundtrip.names == source.names,
            "authoring reflected fields did not roundtrip");
        Check(yamlRoundtrip.labels.at(ce::utf8_string("null")) == ce::string(u"~"),
            "authoring ce keyed map lost reserved strings");

        ce::engine_node_map<ce::string, ce::utf8_string> map;
        map.emplace(ce::string(u"키 \U0001F680"), ce::utf8_string("value"));
        const auto encoded = reflgen::json::to_string(map);
        Check(encoded.front() == '{', "UTF16 keyed map must be a JSON object");
        const auto decoded = reflgen::json::from_string<decltype(map)>(encoded);
        Check(decoded.at(ce::string(u"키 \U0001F680")).view() == ce::utf8_string_view("value"),
            "UTF16 key decoding failed");
        Check(ReadYaml<decltype(map)>(Yaml(map)).size() == 1, "authoring UTF16 keyed map failed");
        reflgen::json::from_string("{\"same\":\"first\",\"same\":\"last\"}", map);
        Check(map.size() == 1 && map.at(ce::string(u"same")).view() == ce::utf8_string_view("last"),
            "duplicate engine map key no longer uses the last value");

        ce::engine_ordered_multimap<ce::utf8_string, int> multi;
        multi.emplace(ce::utf8_string("same"), 1);
        multi.emplace(ce::utf8_string("same"), 2);
        const auto multijson = reflgen::json::to_string(multi);
        Check(multijson.front() == '[', "multimap must preserve duplicate keys as pairs");
        Check(reflgen::json::from_string<decltype(multi)>(multijson).size() == 2,
            "engine string multimap lost duplicates");
    }

    void InvalidUnicode()
    {
        const char16_t badUnit = 0xd800;
        const ce::string invalid(&badUnit, 1);
        std::string written("prefix");
        reflgen::json::writer writer(written);
        bool failed = false;
        try { reflgen::serialize(writer, invalid); }
        catch (const reflgen::serialization_error& error)
        {
            failed = error.message().find("UTF-16") != std::string::npos;
        }
        Check(failed && written == "prefix", "invalid UTF16 mutated the writer before failure");
        ce::string preserved(u"unchanged");
        failed = false;
        try { Meta::StringSerialization::Decode(std::string_view("\xf0\x28\x8c\x28", 4), preserved); }
        catch (const reflgen::serialization_error&) { failed = true; }
        Check(failed && preserved.view() == ce::string_view(u"unchanged"),
            "invalid UTF8 replaced an existing UTF16 value");
        Authoring::WriteDocument malformed;
        malformed.Root().SetString(ce::utf8_string_view("\xed\xa0\x80", 3));
        Authoring::ReflgenReader reader(malformed.Root().Read());
        failed = false;
        try { reflgen::deserialize(reader, preserved); }
        catch (const reflgen::serialization_error&) { failed = true; }
        Check(failed && preserved.view() == ce::string_view(u"unchanged"),
            "authoring reader accepted UTF8 encoding a surrogate or replaced its destination");
        Strings nested;
        nested.names.push_back(invalid);
        failed = false;
        try { (void)reflgen::json::to_string(nested); }
        catch (const reflgen::serialization_error& error) { failed = error.path() == "/names/0"; }
        Check(failed, "nested invalid Unicode did not retain its reflection error path");
    }

    void MethodText()
    {
        Methods object;
        const auto& descriptor = reflgen::type_descriptor_of<Methods>();
        std::any utf16 = ce::string(u"메서드 \U0001F680");
        std::any utf8 = ce::utf8_string("메서드 \xf0\x9f\x9a\x80");
        std::any legacy = std::string("메서드 \xf0\x9f\x9a\x80");
        const auto invoke = [&](const char* name, std::any& input, Meta::MethodInputKind kind)
        {
            const auto* method = descriptor.find_method(name);
            Check(method != nullptr, "reflected method missing");
            Check(Meta::MethodInputOf(method->parameters()[0].type()) == kind,
                "method parameter type dispatch does not recognize engine string");
            void* argument[] = {Meta::MethodArgumentAddress(input)};
            Check(argument[0] != nullptr, "method argument does not point to its engine value");
            return Meta::InvokeAndDescribe(&object, *method, argument);
        };
        const auto expected = "Result: 메서드 \xf0\x9f\x9a\x80";
        Check(invoke("Utf16", utf16, Meta::MethodInputKind::Utf16String) == expected,
            "UTF16 method invocation/result conversion failed");
        Check(invoke("Utf8", utf8, Meta::MethodInputKind::Utf8String) == expected,
            "UTF8 method invocation/result conversion failed");
        Check(invoke("Legacy", legacy, Meta::MethodInputKind::String) == expected,
            "standard method invocation changed");
        bool failed = false;
        try { (void)Meta::InvokeAndDescribe(&object, *descriptor.find_method("Invalid"), {}); }
        catch (const std::invalid_argument&) { failed = true; }
        Check(failed, "invalid UTF16 method result was silently displayed");
    }
}

int main()
{
    try
    {
        Scalars();
        ContainersAndRecords();
        InvalidUnicode();
        MethodText();
        std::printf("[REFLECTION STRING] PASS: %u checks\n", checks);
        return 0;
    }
    catch (const std::exception& error)
    {
        std::fprintf(stderr, "[REFLECTION STRING] FAIL: %s\n", error.what());
        return 1;
    }
}
