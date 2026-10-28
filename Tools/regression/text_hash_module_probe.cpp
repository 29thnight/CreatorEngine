#include "../../Engine/Utility_Framework/Foundation/StandardLibrary.h"
#if defined(CE_CONTRACT_STANDALONE)
#include "../build/ModuleAdapters/Creator.Core.Sha256.h"
#include "../build/ModuleAdapters/Creator.Core.Strings.h"
#include "../build/ModuleAdapters/Creator.Platform.Windows.Strings.h"
#else
import std;
import Creator.Core.Sha256;
import Creator.Core.Strings;
import Creator.Platform.Windows.Strings;
#endif

#include "../../Engine/CommandService/HttpRequest.h"
#include "../../Editor/EngineEntry/Scripting/EditorComponentCatalog.h"

std::array<std::uint8_t, 32> text_cng_reference(std::span<const std::uint8_t> bytes);

ce::crypto::sha256_digest text_hash_identity(std::string_view text);
std::string text_lower_identity(std::string_view text);

int main()
{
    unsigned checks{};
    const auto check = [&](bool value)
    {
        ++checks;
        if (!value)
        {
            throw std::runtime_error("Text/hash module contract failed: " + std::to_string(checks));
        }
    };
    const auto hex = [](std::string_view text)
    { return ce::crypto::to_hex(ce::crypto::sha256::compute(text.data(), text.size())); };
    check(hex("") == "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
    check(hex("abc") == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    check(hex(std::string(1000000, 'a')) == "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0");
    static_assert(sizeof(ce::crypto::sha256_digest) == 32);
    for (std::size_t size : {0u, 1u, 55u, 56u, 63u, 64u, 65u, 127u, 128u, 129u, 65535u, 65536u, 65537u})
    {
        std::vector<std::uint8_t> bytes(size);
        for (std::size_t i = 0; i < size; ++i)
        {
            bytes[i] = static_cast<std::uint8_t>(i * 17u);
        }
        const auto expected = text_cng_reference(bytes);
        check(expected.size() == 32);
        check(ce::crypto::sha256::compute(bytes.data(), bytes.size()) == expected);
        for (std::size_t chunk : {1u, 13u, 64u, 65536u})
        {
            ce::crypto::sha256 hash;
            hash.update(nullptr, 0);
            for (std::size_t offset = 0; offset < bytes.size(); offset += chunk)
            {
                hash.update(bytes.data() + offset, (std::min)(chunk, bytes.size() - offset));
            }
            check(hash.finish() == expected);
        }
    }
    check(text_hash_identity("abc") == ce::crypto::sha256::compute("abc", 3));
    check(text_lower_identity("ABC") == "abc");
    check(ce::strings::to_lower(std::string_view("ABCD", 2)) == "ab");
    check(ce::strings::to_lower(std::string_view("A\0B", 3)) == std::string("a\0b", 3));
    check(ce::strings::trim(" \t\n hello \r\n") == "hello");
    check(ce::strings::trim(" \t\n").empty());
    check(ce::strings::split("a,,b,", ',') == std::vector<std::string>({"a", "", "b"}));
    check(ce::strings::split("", ',').empty());
    check(ce::strings::tokenize_preserve_quotes(" a 'b c' \"d e\" ") ==
          std::vector<std::string>({"a", "'b c'", "\"d e\""}));
    check(ce::strings::unquote("'hello world'") == "hello world");
    check(ce::strings::unquote("'partial") == "'partial");
    std::string node, slot;
    check(ce::strings::split_endpoint(" Node . Slot ", node, slot) && node == "Node" && slot == "Slot");
    check(!ce::strings::split_endpoint("missing", node, slot));
    check(!ce::strings::split_endpoint(" . slot", node, slot));
    const std::string utf8 = "\xED\x95\x9C\xEA\xB8\x80 \xF0\x9F\x98\x80";
    const std::wstring wide = L"\uD55C\uAE00 \U0001F600";
    check(ce::strings::utf8_to_wide(utf8) == wide);
    check(ce::strings::wide_to_utf8(wide) == utf8);
    check(ce::strings::wide_to_utf8(ce::strings::utf8_to_wide(std::string("a\0b", 3))) == std::string("a\0b", 3));
    check(ce::strings::utf8_to_wide("").empty() && ce::strings::wide_to_utf8(L"").empty());
    check(ce::strings::utf8_to_ansi("ASCII") == "ASCII");
    check(ce::strings::utf8_to_ansi(std::string("\xFF", 1)).empty());
    // Retain the pre-migration ANSI helper's terminating NUL representation.
    check(ce::strings::ansi_to_utf8("ASCII") == std::string("ASCII\0", 6));
    check(ce::strings::ansi_to_utf8("") == std::string(1, '\0'));
    check(ce::strings::trim_view(" \thello \t", " \t") == "hello");
    check(ce::strings::trim_view("\rhello\n", " \t") == "\rhello\n");
    check(ce::strings::trim("\vhello\f", " \t\r\n") == "\vhello\f");
    check(ce::strings::trim_view("\vhello\f") == "hello");
    check(ce::strings::equals_ascii_case_insensitive("VuLkAn", "vulkan"));
    check(!ce::strings::equals_ascii_case_insensitive("vulkan!", "vulkan"));
    const std::string high_byte("A\xC0", 2);
    check(ce::strings::ascii_to_lower(high_byte) == std::string("a\xC0", 2));
    check(ce::strings::to_lower_without_whitespace(" Mesh\tRenderer ") == "meshrenderer");
    check(ce::strings::contains_case_insensitive("MeshRenderer", "rEnD"));
    check(ce::strings::contains_case_insensitive("text", static_cast<const char*>(nullptr)));
    check(!ce::strings::contains_case_insensitive("text", "other"));
    std::wstring wide_sentinel = L"preserved";
    check(!ce::strings::try_utf8_to_wide(std::string("\xFF", 1), wide_sentinel) && wide_sentinel == L"preserved");
    std::string utf8_sentinel = "preserved";
    check(!ce::strings::try_wide_to_utf8(std::wstring(1, static_cast<wchar_t>(0xD800)), utf8_sentinel) &&
          utf8_sentinel == "preserved");
    check(ce::strings::utf8_to_wide(static_cast<const char*>(nullptr)).empty());
    check(ce::strings::wide_to_utf8(static_cast<const wchar_t*>(nullptr)).empty());
    check(editor::components::Matches(editor::components::Native("MeshRenderer"), "mEsH reN"));
    check(!editor::components::Matches(editor::components::Native("MeshRenderer"), "mesh audio"));
    const auto http = CommandService::ParseHttpRequest(
        "GET /status HTTP/1.1\r\nHoSt: \tlocalhost \t\r\nConnection: ClOsE\r\n\r\n", {});
    check(http.status == CommandService::HttpParseStatus::Ok);
    check(http.request.Header("HOST") && *http.request.Header("HOST") == "localhost" && !http.request.keepAlive);
    std::printf("TEXT_HASH_MODULES_OK checks=%u sha_vectors=3 cng_lengths=13 cross_tu=true\n", checks);
}
