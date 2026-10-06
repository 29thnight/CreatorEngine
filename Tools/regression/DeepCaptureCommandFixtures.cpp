// Source fixture only: UNEXECUTED. Requires the Editor command registration TU
// and its normal link closure. This file does not start an Editor or a collector.
#include "Commands/CommandRegistrar.h"
#include "CommandCore/CommandDescriptorSeeds.h"

#include <array>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <string_view>
#include <unordered_set>

namespace deep_capture_command_fixtures
{
    void check(bool value, const char* message)
    {
        if (!value)
        {
            std::fprintf(stderr, "DeepCaptureCommandFixtures: %s\n", message);
            std::abort();
        }
    }

    constexpr std::array<std::string_view, 3> deep_commands{
        "profile.deep.start", "profile.deep.status", "profile.deep.stop"
    };

    class recording_registrar final : public ConsoleCmd::Registrar
    {
    public:
        void Result(std::initializer_list<const char*> names, ConsoleCommandResultHandler handler) override
        {
            remember(names, handler);
        }

        void Escaping(std::initializer_list<const char*> names, ConsoleCommandResultHandler handler) override
        {
            remember(names, handler);
        }

        std::unordered_set<std::string> registered;
        std::array<unsigned, deep_commands.size()> counts{};

    private:
        void remember(std::initializer_list<const char*> names, ConsoleCommandResultHandler handler)
        {
            check(handler != nullptr, "registered result handler exists");
            for (const auto* name : names)
            {
                check(registered.emplace(name).second, "canonical and alias names never collide");
                for (std::size_t index = 0; index != deep_commands.size(); ++index)
                {
                    if (name == deep_commands[index])
                    {
                        check(names.size() == 1, "deep controls have no unadvertised aliases");
                        ++counts[index];
                    }
                }
            }
        }
    };

    void lexical_seed_order_and_exact_registration()
    {
        using namespace CommandCore;
        std::string_view previous;
        for (std::size_t index = 0; index != DescriptorSeedCount(); ++index)
        {
            const auto* seed = DescriptorSeedAt(index);
            check(seed != nullptr, "every seed index resolves");
            check(previous.empty() || previous < seed->name, "seed names are strictly sorted and unique");
            check(FindDescriptorSeed(seed->name) == seed, "binary lookup returns the exact seed");
            previous = seed->name;
        }
        recording_registrar registrar;
        ConsoleCmd::RegisterDiagnosticsCommands(registrar);
        for (std::size_t index = 0; index != deep_commands.size(); ++index)
        {
            check(registrar.counts[index] == 1, "each deep command is registered exactly once");
            const auto* seed = FindDescriptorSeed(deep_commands[index]);
            check(seed != nullptr && seed->cost == CommandCost::Immediate &&
                  seed->roles == CommandRoles::Editor && seed->liveness == CommandLiveness::Live &&
                  seed->cls == CommandClass::EngineService && !seed->executesUserCode,
                  "deep controls preserve the bounded live Editor command contract");
            check(!std::string_view(seed->summary).empty(), "discovery and help share a nonempty summary");
            check(std::string_view(seed->namedParameters) == (index == 2 ? "sessionId" : "()"),
                  "HTTP accepts only the full decimal string session ID for Stop, no inputs for Start/Status");
        }
    }
}

int main()
{
    deep_capture_command_fixtures::lexical_seed_order_and_exact_registration();
    return 0;
}
