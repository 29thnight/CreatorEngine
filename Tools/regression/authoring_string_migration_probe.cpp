#include "../../Engine/Utility_Framework/Authoring/AuthoringParsedDocument.h"
#include "../../Engine/Utility_Framework/Authoring/AuthoringCookedDocument.h"
#include "../../Engine/Utility_Framework/Authoring/AuthoringParseTelemetry.h"
#include "../../Engine/Utility_Framework/Authoring/AuthoringRymlErrorPolicy.h"

#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <type_traits>

namespace
{
    static_assert(requires(Authoring::WriteNode writer, Authoring::ReadNode reader, ce::string output)
    {
        writer.Child({});
        writer.RemoveChild({});
        writer.SetString({});
        writer.SetString({"text", 4});
        writer.SetScalar("literal");
        writer.SetScalar(ce::utf8_string_view("literal"));
        writer.SetScalar(ce::string_view(u"literal"));
        reader.operator[]({});
        reader.HasChild({});
        Authoring::Scalar::TryConvert({}, output);
    });

    unsigned checks = 0;

    void Check(bool passed, const char* message)
    {
        ++checks;
        if (!passed) throw std::runtime_error(message);
    }

    struct WorkDirectory
    {
        std::filesystem::path path = std::filesystem::temp_directory_path() /
            ("creator-authoring-string-" + std::to_string(
                std::chrono::steady_clock::now().time_since_epoch().count()));
        WorkDirectory() { std::filesystem::create_directories(path); }
        ~WorkDirectory()
        {
            std::error_code ignored;
            std::filesystem::remove_all(path, ignored);
        }
    };

    ce::utf8_string Utf8Path(const std::filesystem::path& path)
    {
        const auto bytes = path.u8string();
        ce::utf8_string text;
        text.resize_and_overwrite(bytes.size(), [&](char* output, std::size_t count)
        {
            if (count) std::memcpy(output, bytes.data(), count);
            return count;
        });
        return text;
    }

    void Write(const std::filesystem::path& path, const char* bytes, std::size_t count)
    {
        std::ofstream output(path, std::ios::binary);
        output.write(bytes, static_cast<std::streamsize>(count));
        output.close();
        Check(bool(output), "fixture write failed");
    }

    void TextAndLifetime()
    {
        ce::utf8_string failure("stale diagnostic");
        ce::utf8_string source("name: 한글 \xf0\x9f\x9a\x80\nquoted: \"a\\0b\"\n");
        auto document = Authoring::ParsedDocument::ParseText(source.view(), failure);
        Check(bool(document) && failure.empty(), "UTF-8 document parse failed");
        source.assign("destroyed source");
        Check(document.Root()["name"].Scalar() == "한글 \xf0\x9f\x9a\x80", "parser did not own UTF-8 scalar");
        const auto nul = document.Root()["quoted"].Scalar();
        Check(nul.size() == 3 && nul[0] == 'a' && nul[1] == '\0' && nul[2] == 'b',
              "length-based YAML scalar lost embedded NUL");
        auto moved = std::move(document);
        Check(moved.Root()["name"].Scalar() == "한글 \xf0\x9f\x9a\x80", "moving parsed document lost text");

        Check(!Authoring::ParsedDocument::ParseText("a: [unterminated", failure) && !failure.empty(),
              "malformed YAML failed without a UTF-8 diagnostic");
        Check(bool(Authoring::ParsedDocument::ParseText("a: ok", failure)) && failure.empty(),
              "successful parse retained stale failure");
        ce::utf8_string aliased("alias: 한글");
        const auto aliasDocument = Authoring::ParsedDocument::ParseText(aliased.view(), aliased);
        Check(bool(aliasDocument) && aliased.empty() && aliasDocument.Root()["alias"].Scalar() == "한글",
              "error output invalidated an aliased input before parsing");
        std::string boundaryFailure;
        const std::string boundaryText = "boundary: UTF-8 한글";
        const auto boundary = Authoring::ParsedDocument::ParseText(boundaryText, boundaryFailure);
        Check(bool(boundary) && boundaryFailure.empty() && boundary.Root()["boundary"].Scalar() == "UTF-8 한글",
              "standard serialization boundary changed");
        const auto attempt = Authoring::TryParseWithPolicy(ce::utf8_string_view("a: [broken"));
        Check(attempt.threw && !attempt.message.empty(), "error policy lost engine-owned diagnostic");
    }

    void FilesAndCookedDispatch()
    {
        WorkDirectory work;
        const auto path = work.path / std::filesystem::path(u8"한글-\U0001F680.yaml");
        ce::utf8_string source("text: \"");
        for (unsigned index = 0; index != 8192; ++index) source.append("한글");
        source.append("\"\n");
        Write(path, source.data(), source.size());
        ce::utf8_string failure;
        const auto utf8Path = Utf8Path(path);
        const auto fromUtf8 = Authoring::ParsedDocument::ParseFile(utf8Path.view(), failure);
        Check(bool(fromUtf8) && failure.empty(), "UTF-8 filesystem path failed");
        auto aliasPath = utf8Path;
        Check(bool(Authoring::ParsedDocument::ParseFile(aliasPath.view(), aliasPath)) && aliasPath.empty(),
              "error output invalidated an aliased UTF-8 path");
        Check(fromUtf8.Root()["text"].Scalar().size() == 49152, "multi-chunk UTF-8 file was truncated");
        const auto native = Authoring::ParsedDocument::ParseFilePath(path, failure);
        Check(bool(native) && native.Root()["text"].Scalar() == fromUtf8.Root()["text"].Scalar(),
              "native and UTF-8 path results differ");
        const auto mutableDocument = Authoring::WriteDocument::ParseFile(path, failure);
        Check(bool(mutableDocument) && failure.empty()
              && mutableDocument->Root()["text"].Scalar() == fromUtf8.Root()["text"].Scalar(),
              "mutable authoring document lost native path or long UTF-8 text");
        std::string boundaryFailure;
        const auto boundary = Authoring::ParsedDocument::ParseFile(
            std::string(utf8Path.data(), utf8Path.size()), boundaryFailure);
        Check(bool(boundary) && boundaryFailure.empty(), "standard UTF-8 path boundary failed");
        const auto mutableBoundary = Authoring::WriteDocument::ParseText(
            std::string_view(source.data(), source.size()), &boundaryFailure);
        Check(bool(mutableBoundary) && boundaryFailure.empty(), "mutable standard text boundary changed");

        auto nulPath = utf8Path;
        nulPath.push_back('\0');
        nulPath.append("ignored-suffix");
        Check(!Authoring::ParsedDocument::ParseFile(nulPath.view(), failure) && !failure.empty(),
              "embedded NUL path silently opened a prefix");
        Check(!Authoring::ParsedDocument::ParseFilePath(work.path / "missing.yaml", failure) && !failure.empty(),
              "missing file did not return a diagnostic");

        Authoring::WriteDocument writer;
        writer.Root().SetMap();
        writer.Root().Child("text").SetString("cooked 한글");
        ce::dynamic_array<std::byte> cooked;
        Check(Authoring::EncodeCookedDocument(writer.Root().Read(), cooked, boundaryFailure), "cooked fixture encode failed");
        const auto cookedPath = work.path / "fixture.asset";
        Write(cookedPath, reinterpret_cast<const char*>(cooked.data()), cooked.size());
        const auto before = Authoring::GetTextParseTelemetry().calls;
        const auto parsedCooked = Authoring::ParsedDocument::ParseFilePath(cookedPath, failure);
        Check(bool(parsedCooked) && parsedCooked.Root()["text"].Scalar() == "cooked 한글",
              "binary file dispatch lost cooked scalar");
        Check(Authoring::GetTextParseTelemetry().calls == before, "cooked file entered YAML parser");
        Check(!Authoring::ParsedDocument::ParseCooked(std::span(cooked.data(), cooked.size() - 1), failure),
              "truncated binary document accepted");
        Check(Authoring::GetTextParseTelemetry().calls == before, "invalid binary document entered YAML parser");

        const auto modelPath = work.path / "model.asset";
        const char modelBytes[] = {'C', 'E', 'M', 'A', '\0', '\xff'};
        Write(modelPath, modelBytes, sizeof(modelBytes));
        Check(!Authoring::ParsedDocument::ParseFilePath(modelPath, failure), "CEMA binary parsed as text");
        Check(Authoring::GetTextParseTelemetry().calls == before, "CEMA binary entered YAML parser");
        const auto telemetry = Authoring::GetTextParseTelemetry();
        bool found = false;
        for (const auto& context : telemetry.contexts)
        {
            static_assert(std::is_same_v<std::remove_cvref_t<decltype(context)>, ce::utf8_string>);
            if (context == utf8Path) found = true;
        }
        Check(found, "UTF-8 path telemetry was not preserved");
    }

    void EngineScalarRoundTrip()
    {
        Authoring::WriteDocument writer;
        auto root = writer.Root();
        root.SetMap();
        ce::utf8_string key("한글-key");
        ce::utf8_string utf8("한글 \xf0\x9f\x9a\x80");
        ce::string utf16(u"UTF-16 한글 \U0001F680");
        root.Child(key).SetString(utf8);
        root.Child("wide").SetString(utf16);
        root.Child("scalar").SetScalar(utf16);
        root.Child("literal").SetScalar(u"UTF-16 literal");
        root.Child("null-text").SetString(ce::string(u"null"));
        const char16_t nulUnits[] = {u'a', u'\0', u'b'};
        root.Child("nul-wide").SetString(ce::string_view(nulUnits, 3));
        auto unchanged = root.Child("unchanged");
        unchanged.SetString("preserved");
        const char16_t invalidUnits[] = {0xd800};
        bool rejected = false;
        try { unchanged.SetString(ce::string_view(invalidUnits, 1)); }
        catch (const std::invalid_argument&) { rejected = true; }
        Check(rejected && unchanged.Read().Scalar() == "preserved", "invalid UTF-16 changed a writer node");
        key.clear();
        Check(root.Read().HasChild(ce::utf8_string_view("한글-key")), "writer retained a borrowed UTF-8 key");
        const auto text = writer.Dump();
        ce::utf8_string failure;
        const auto parsed = Authoring::ParsedDocument::ParseText({text.data(), text.size()}, failure);
        Check(bool(parsed) && failure.empty(), "engine string YAML round trip failed");
        Check(parsed.Root()[ce::utf8_string_view("한글-key")].As<ce::utf8_string>() == utf8,
              "UTF-8 scalar round trip failed");
        Check(parsed.Root()["wide"].As<ce::string>() == utf16, "UTF-16 scalar round trip failed");
        Check(parsed.Root()["scalar"].As<ce::string>() == utf16, "UTF-16 scalar writer dispatch failed");
        Check(parsed.Root()["literal"].As<ce::string>() == ce::string_view(u"UTF-16 literal"),
              "UTF-16 literal was dispatched as bool");
        Check(!parsed.Root()["null-text"].IsNull() && parsed.Root()["null-text"].As<ce::string>() == ce::string_view(u"null"),
              "typed UTF-16 text was reinterpreted as YAML null");
        Check(parsed.Root()["nul-wide"].As<ce::string>().view() == ce::string_view(nulUnits, 3),
              "UTF-16 scalar embedded NUL was truncated");

        ce::string preserved(u"preserved");
        const char invalidBytes[] = {char(0xc0), char(0x80)};
        Check(!Authoring::Scalar::TryConvert(ce::utf8_string_view(invalidBytes, 2), preserved)
              && preserved == ce::string_view(u"preserved"), "invalid UTF-8 replaced scalar output");
        root.Child("invalid").SetString(ce::utf8_string_view(invalidBytes, 2));
        Check(root.Read()["invalid"].As(ce::string(u"fallback")) == ce::string_view(u"fallback"),
              "invalid UTF-8 scalar did not return fallback");
        rejected = false;
        try { (void)root.Read()["invalid"].As<ce::string>(); }
        catch (const std::runtime_error&) { rejected = true; }
        Check(rejected, "invalid UTF-8 scalar silently converted to UTF-16");
        Check(root.Read().As(ce::utf8_string("fallback")) == ce::utf8_string_view("fallback"),
              "container node was accepted as string scalar");
        Check(root.RemoveChild(ce::utf8_string_view("한글-key")) && !root.Read().HasChild("한글-key"),
              "UTF-8 child removal failed");
    }
}

int main()
{
    try
    {
        TextAndLifetime();
        FilesAndCookedDispatch();
        EngineScalarRoundTrip();
        std::printf("AUTHORING_STRING_MIGRATION_PASS checks=%u\n", checks);
        return 0;
    }
    catch (const std::exception& error)
    {
        std::fprintf(stderr, "AUTHORING_STRING_MIGRATION_FAIL %s\n", error.what());
        return 1;
    }
}
