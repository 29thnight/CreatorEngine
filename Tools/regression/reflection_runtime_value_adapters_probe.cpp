#include "../../Engine/Containers/Creator.Containers.h"
#include "../../Engine/Utility_Framework/Reflection/ScriptReflectionPrimitives.h"
#include "../../Engine/SceneRuntime/ScriptReflectionAudioValues.h"
#include <reflgen/json/reader.h>
#include <reflgen/json/writer.h>
#include <cstdio>
#include <stdexcept>

namespace
{
    unsigned checks = 0;

    void Check(bool condition, const char* message)
    {
        ++checks;
        if (!condition)
        {
            throw std::runtime_error(message);
        }
    }

    template<class T> std::string Write(const T& value)
    {
        std::string result;
        reflgen::json::writer writer(result);
        reflgen::value_descriptor_of<T>().serialize(writer, &value);
        return result;
    }

    template<class T> void Read(std::string_view text, T& value)
    {
        reflgen::json::reader reader(text);
        reflgen::value_descriptor_of<T>().deserialize(reader, &value);
        reader.finish();
    }

    void StandardValues()
    {
        Check(Write(std::strong_ordering::less) == "-1", "ordering less value changed");
        Check(Write(std::strong_ordering::equal) == "0", "ordering equal value changed");
        Check(Write(std::strong_ordering::greater) == "1", "ordering greater value changed");
        Check(!reflgen::value_descriptor_of<std::strong_ordering>().is_assignable(),
              "a comparison result must not expose object mutation");

        using Time = std::filesystem::file_time_type;
        const Time oldTime(Time::duration(-1234567890123LL));
        Time decoded;
        Read(Write(oldTime), decoded);
        Check(decoded == oldTime, "native file-clock epoch or precision changed");
        const Time limit{Time::duration{(std::numeric_limits<Time::duration::rep>::max)()}};
        Read(Write(limit), decoded);
        Check(decoded == limit, "maximum file-clock ticks lost precision");
        bool rejected = false;
        try
        {
            Read("18446744073709551615", decoded);
        }
        catch (const reflgen::serialization_error&)
        {
            rejected = true;
        }
        Check(rejected && decoded == limit, "overflow changed the previous file timestamp");

        std::atomic<bool> completed(false);
        Check(Write(completed) == "false", "atomic false snapshot failed");
        completed.store(true, std::memory_order_release);
        Check(Write(completed) == "true", "atomic publication snapshot failed");
        const auto& atomic = reflgen::value_descriptor_of<std::atomic<bool>>();
        Check(!atomic.is_deserializable() && !atomic.is_assignable(),
              "script must not manufacture completion by rewriting an atomic flag");
    }

    void AudioIdentity()
    {
        const wave::ClipKey text("music\\intro");
        wave::ClipKey decoded;
        Read(Write(text), decoded);
        Check(decoded == text && !decoded.IsGuid(), "legacy clip text identity was lost");

        const auto guid = uuid::parse("12345678-1234-4234-8234-123456789abc");
        const wave::ClipKey authoredGuid = wave::ClipKey::FromGuid(guid);
        Read(Write(authoredGuid), decoded);
        Check(decoded == authoredGuid && decoded.IsGuid(), "GUID clip identity was lost");

        const wave::ClipKey sameText(authoredGuid.Text());
        Read(Write(sameText), decoded);
        Check(decoded == sameText && decoded != authoredGuid && !decoded.IsGuid(),
              "text equal to a GUID was incorrectly promoted to asset identity");

        for (const std::string_view invalid : {"{}", "{\"text\":\"x\"}",
             "{\"text\":\"x\",\"isGuid\":false,\"isGuid\":false}",
             "{\"text\":\"00000000-0000-0000-0000-000000000000\",\"isGuid\":true}"})
        {
            decoded = sameText;
            bool rejected = false;
            try
            {
                Read(invalid, decoded);
            }
            catch (const std::exception&)
            {
                rejected = true;
            }
            Check(rejected && decoded == sameText, "invalid clip wire data changed prior identity");
        }
    }
}

int main()
{
    try
    {
        StandardValues();
        AudioIdentity();
        std::printf("NATIVE_RUNTIME_VALUE_ADAPTERS_OK checks=%u\n", checks);
        return 0;
    }
    catch (const std::exception& error)
    {
        std::fprintf(stderr, "NATIVE_RUNTIME_VALUE_ADAPTERS_FAILED %s\n", error.what());
        return 1;
    }
}
