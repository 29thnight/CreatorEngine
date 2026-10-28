#include "../../Engine/Utility_Framework/Authoring/DiagnosticDocument.h"

#include <cstdio>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>

namespace
{
    unsigned checks = 0;

    void Check(bool passed, const char* message)
    {
        ++checks;
        if (!passed) throw std::runtime_error(message);
    }

    void JsonTextAndOwnership()
    {
        Diagnostics::Document document;
        auto root = document.Root();
        root.SetMap();
        {
            ce::utf8_string key("한글 \xf0\x9f\x9a\x80");
            ce::utf8_string value("UTF-8 remains UTF-8: 한글 \xf0\x9f\x98\x80");
            root[key] << value;
            key.clear();
            value.assign("changed");
        }
        const char textWithNul[] = {'a', '\0', 'b'};
        root["nul"] << ce::utf8_string_view(textWithNul, sizeof(textWithNul));
        root["escape"] << ce::utf8_string_view("\"\\\b\f\n\r\t\x01");
        root["empty"] << ce::utf8_string_view{};
        root["true"] << true;
        root["false"] << false;
        root["number-text"] << ce::utf8_string_view("123");
        root["maximum"] << (std::numeric_limits<std::uint64_t>::max)();
        root["minimum"] << (std::numeric_limits<std::int64_t>::min)();
        root["fraction"] << 0.125;

        const std::string boundaryKey = "standard-boundary";
        const std::string boundaryValue = "length preserved";
        root[boundaryKey] << boundaryValue;
        Check(root.Has(std::string_view(boundaryKey)), "standard view lookup failed");
        Check(root.Has(ce::utf8_string_view("한글 \xf0\x9f\x9a\x80")), "owned UTF-8 key was lost");
        Check(!root.Has("missing"), "missing UTF-8 key found");
        root["sequence"].SetSequence();
        root["sequence"].Append() << ce::utf8_string_view("first");
        root["sequence"].Append() << ce::utf8_string_view("second");
        root["sequence"][0] << ce::utf8_string_view("first");
        root["map"].SetMap();

        static_assert(std::is_same_v<decltype(document.ToJson()), ce::utf8_string>);
        const auto json = document.ToJson();
        const ce::utf8_string_view expected(
            "{\"한글 \xf0\x9f\x9a\x80\":\"UTF-8 remains UTF-8: 한글 \xf0\x9f\x98\x80\","
            "\"nul\":\"a\\u0000b\",\"escape\":\"\\\"\\\\\\b\\f\\n\\r\\t\\u0001\","
            "\"empty\":\"\",\"true\":true,\"false\":false,\"number-text\":\"123\","
            "\"maximum\":18446744073709551615,\"minimum\":-9223372036854775808,"
            "\"fraction\":0.125,\"standard-boundary\":\"length preserved\","
            "\"sequence\":[\"first\",\"second\"],\"map\":{}}");
        Check(json.view() == expected, "JSON wire format or escaping changed");
        Check(json.c_str()[json.size()] == '\0', "JSON output lacks terminator");
    }

    void LongTextAndStableNodes()
    {
        Diagnostics::Document original;
        auto root = original.Root();
        root.SetMap();
        auto retained = root["retained"];
        retained.SetMap();
        ce::utf8_string longValue;
        for (unsigned index = 0; index != 8192; ++index) longValue.append("한글");
        retained["text"] << longValue;
        for (unsigned index = 0; index != 1024; ++index)
            root[std::to_string(index)] << index;
        Diagnostics::Document moved(std::move(original));
        retained["after-move"] << ce::utf8_string_view("still valid");
        longValue.clear();
        const auto json = moved.ToJson();
        Check(json.view().starts_with("{\"retained\":{\"text\":\"한글"), "long owned value was lost");
        Check(json.size() > 49152, "long UTF-8 text was truncated");
        Check(json.find("\"after-move\":\"still valid\"") != ce::utf8_string_view::npos,
              "diagnostic node did not survive move and growth");

        bool rejected = false;
        try { (void)original.ToJson(); }
        catch (const std::logic_error&) { rejected = true; }
        Check(rejected, "moved-from document accepted output");
        rejected = false;
        try { retained["infinite"] << std::numeric_limits<double>::infinity(); }
        catch (const std::invalid_argument&) { rejected = true; }
        Check(rejected, "nonfinite diagnostic number accepted");
    }
}

int main()
{
    try
    {
        JsonTextAndOwnership();
        LongTextAndStableNodes();
        std::printf("DIAGNOSTIC_STRING_MIGRATION_PASS checks=%u\n", checks);
        return 0;
    }
    catch (const std::exception& error)
    {
        std::fprintf(stderr, "DIAGNOSTIC_STRING_MIGRATION_FAIL %s\n", error.what());
        return 1;
    }
}
