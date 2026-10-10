#include "InputGraphAssetCodecSelfTest.h"
#include "Experiment/Cooked/CookedInputGraph.h"
#include "InputBindingOverrideArchive.h"

namespace InputTests
{
    bool RunAssetCodecSelfTest(std::string& failure)
    {
        namespace cooked = experiment::cooked;
        experiment::AssetId identity;
        if (!experiment::TryParseCanonicalAssetId("9ad58e30-9ff7-4f2a-a8c2-435a3c126801", identity))
        {
            failure = "Self-test identity could not be parsed.";
            return false;
        }
        Input::InputGraph graph;
        graph.id = cooked::InputGraphIdentity(identity);
        graph.layers.push_back({ { 1u, 1u }, "Gameplay" });
        Input::InputSignalDefinition signal;
        signal.id = { 2u, 2u };
        signal.layer = graph.layers.front().id;
        signal.name = "Jump";
        graph.signals.push_back(signal);
        Input::InputBinding binding;
        binding.id = { 3u, 3u };
        binding.signal = signal.id;
        binding.sources.push_back({ { Input::SourceKind::Key, 0x39u }, 1.0f });
        graph.bindings.push_back(binding);
        std::vector<Input::InputDiagnostic> diagnostics;
        const auto program = Input::CompileInputGraph(graph, diagnostics);
        if (!program)
        {
            failure = "Self-test definition did not compile.";
            return false;
        }
        std::vector<std::byte> bytes;
        auto decoded = program;
        if (!cooked::WriteInputGraphArtifact(*program, bytes, failure) ||
            !cooked::ReadInputGraphArtifact(bytes, identity, decoded, failure, 7u) ||
            decoded->GetSemanticHash() != program->GetSemanticHash() || decoded->GetDefinition().generation != 7u ||
            decoded->GetDefinition().bindings.front().id != binding.id)
        {
            failure = "InputGraph cooked round-trip did not retain identity/hash/generation.";
            return false;
        }
        const auto lastGood = decoded;
        for (std::size_t size = 0; size < bytes.size(); ++size)
        {
            if (cooked::ReadInputGraphArtifact(std::span(bytes).first(size), identity, decoded, failure) || std::addressof(*decoded) != std::addressof(*lastGood))
            {
                failure = "A truncated InputGraph artifact replaced the last good lease.";
                return false;
            }
        }
        // Every version/capability field independently rejects unknown data.
        for (const std::size_t offset : { 4u, 8u, 12u, 16u, 20u })
        {
            auto changed = bytes;
            changed[offset] = std::byte{ 0x7fu };
            if (cooked::ReadInputGraphArtifact(changed, identity, decoded, failure) || std::addressof(*decoded) != std::addressof(*lastGood))
            {
                failure = "An incompatible InputGraph version/capability was accepted.";
                return false;
            }
        }
        auto trailing = bytes;
        trailing.push_back(std::byte{});
        if (cooked::ReadInputGraphArtifact(trailing, identity, decoded, failure))
        {
            failure = "Trailing artifact bytes were accepted.";
            return false;
        }
        graph.generation = 55u;
        graph.layers.front().name = "Display-only layer rename";
        graph.signals.front().name = "Display-only signal rename";
        const auto renamed = Input::CompileInputGraph(graph, diagnostics);
        if (!renamed || renamed->GetSemanticHash() != program->GetSemanticHash())
        {
            failure = "Display names or publication generation affected semantic identity.";
            return false;
        }
        Input::InputBindingOverride replacement;
        replacement.graph = graph.id;
        replacement.binding = binding.id;
        replacement.sources.push_back({ { Input::SourceKind::Key, 0x1eu }, 1.0f });
        const std::vector overrides{ replacement };
        std::vector<std::byte> profile;
        std::vector<Input::InputBindingOverride> restored;
        if (!Input::WriteInputBindingOverrides(17u, *program, overrides, profile, failure) ||
            !Input::ReadInputBindingOverrides(profile, 17u, *program, restored, failure) ||
            restored.size() != 1u || restored.front().binding != binding.id || restored.front().sources.front().control.code != 0x1eu)
        {
            failure = "Stable-ID user override round-trip failed.";
            return false;
        }
        if (Input::ReadInputBindingOverrides(profile, 18u, *program, restored, failure))
        {
            failure = "An override profile was accepted for a different user.";
            return false;
        }
        graph.bindings.front().id = { 4u, 4u };
        const auto orphaned = Input::CompileInputGraph(graph, diagnostics);
        if (!orphaned || Input::ReadInputBindingOverrides(profile, 17u, *orphaned, restored, failure))
        {
            failure = "An orphan override was silently rebound by name or slot.";
            return false;
        }
        failure.clear();
        return true;
    }
}
