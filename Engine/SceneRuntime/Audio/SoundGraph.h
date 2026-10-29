#include "../../Utility_Framework/Foundation/EngineNodeMap.h"
#include "../../Containers/ContainerAPI.h"
#pragma once
#include "../../Utility_Framework/Foundation/EngineFunc.h"
#if defined(CE_CONTRACT_STANDALONE)
#include "../../../Tools/build/ModuleAdapters/Creator.Core.Ownership.h"
#else
import Creator.Core.Ownership;
#endif

#include "PlayRequest.h"

#include "../../Utility_Framework/Foundation/StandardLibrary.h"

namespace wave
{
    using ParameterValue = std::variant<bool, std::int32_t, float, std::string>;
    using ParameterMap = ce::engine_node_map<std::string, ParameterValue>;

    enum class ParameterType : std::uint8_t { Boolean, Integer, Float, String };
    enum class SoundNodeKind : std::uint8_t { Clip, Random, Switch, Layer, GainPitch, Parameter, Output };
    enum class SoundSourceKind : std::uint8_t { Clip, Preset, Graph };

    struct [[reflgen::runtime, reflgen::public_fields, reflgen::public_methods]] SoundSource final
    {
        SoundSourceKind kind{ SoundSourceKind::Clip };
        ClipKey asset;
    };

    struct ParameterDefinition final
    {
        std::string name;
        ParameterType type{ ParameterType::Float };
        ParameterValue defaultValue{ 0.0f };
    };

    // Inputs are node IDs. Switch case values match inputs, with an optional
    // final default input. Parameter nodes provide a typed value to GainPitch
    // (gainParameter/pitchParameter) or Switch (parameter); they never emit PCM.
    struct SoundNode final
    {
        std::uint32_t id{};
        SoundNodeKind kind{ SoundNodeKind::Clip };
        ce::dynamic_array<std::uint32_t> inputs;
        ClipKey clip;
        ce::dynamic_array<float> weights;
        std::string parameter;
        ce::dynamic_array<ParameterValue> cases;
        float gain{ 1.0f };
        float pitch{ 1.0f };
        std::string gainParameter;
        std::string pitchParameter;
    };

    struct SoundGraphDefinition final
    {
        std::uint32_t schemaVersion{ 1u };
        ce::dynamic_array<ParameterDefinition> parameters;
        ce::dynamic_array<SoundNode> nodes;
        std::uint32_t output{};
        std::uint32_t maximumVoices{ 32u };
    };

    // Published only by CompileSoundGraph. Callers retain an immutable shared
    // program; each PlaybackInstance keeps its own parameters and random seed.
    struct SoundGraphProgram final
    {
        SoundGraphDefinition definition;
        ce::engine_node_map<std::uint32_t, std::size_t> nodeIndices;
        std::uint32_t maximumVoices{};
    };

    struct GraphVoice final
    {
        ClipKey clip;
        float gain{ 1.0f };
        float pitch{ 1.0f };
        std::uint64_t branchKey{};
    };

    struct SoundPreset final
    {
        SoundSource source;
        PlayRequest defaults;
        ParameterMap parameters;
    };

    [[nodiscard]] ParameterType TypeOfParameter(const ParameterValue& value) noexcept;
    [[nodiscard]] own::shared_owner<const SoundGraphProgram> CompileSoundGraph(
        const SoundGraphDefinition& definition, const ce::owned_func<bool(const ClipKey&)>& hasClip,
        std::string& error);
    [[nodiscard]] bool ResolveGraphParameters(const SoundGraphProgram& program,
        const ParameterMap& supplied, ParameterMap& resolved, std::string& error);
    [[nodiscard]] bool EvaluateSoundGraph(const SoundGraphProgram& program, const ParameterMap& parameters,
        std::uint64_t& randomState, ce::dynamic_array<GraphVoice>& voices, std::string& error);
}
