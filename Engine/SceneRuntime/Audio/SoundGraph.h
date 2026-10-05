#pragma once

#include "PlayRequest.h"

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <variant>
#include <vector>

namespace wave
{
    using ParameterValue = std::variant<bool, std::int32_t, float, std::string>;
    using ParameterMap = std::unordered_map<std::string, ParameterValue>;

    enum class ParameterType : std::uint8_t { Boolean, Integer, Float, String };
    enum class SoundNodeKind : std::uint8_t { Clip, Random, Switch, Layer, GainPitch, Parameter, Output };
    enum class SoundSourceKind : std::uint8_t { Clip, Preset, Graph };

    struct SoundSource final
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
        std::vector<std::uint32_t> inputs;
        ClipKey clip;
        std::vector<float> weights;
        std::string parameter;
        std::vector<ParameterValue> cases;
        float gain{ 1.0f };
        float pitch{ 1.0f };
        std::string gainParameter;
        std::string pitchParameter;
    };

    struct SoundGraphDefinition final
    {
        std::uint32_t schemaVersion{ 1u };
        std::vector<ParameterDefinition> parameters;
        std::vector<SoundNode> nodes;
        std::uint32_t output{};
        std::uint32_t maximumVoices{ 32u };
    };

    // Published only by CompileSoundGraph. Callers retain an immutable shared
    // program; each PlaybackInstance keeps its own parameters and random seed.
    struct SoundGraphProgram final
    {
        SoundGraphDefinition definition;
        std::unordered_map<std::uint32_t, std::size_t> nodeIndices;
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
    [[nodiscard]] std::shared_ptr<const SoundGraphProgram> CompileSoundGraph(
        const SoundGraphDefinition& definition, const std::function<bool(const ClipKey&)>& hasClip,
        std::string& error);
    [[nodiscard]] bool ResolveGraphParameters(const SoundGraphProgram& program,
        const ParameterMap& supplied, ParameterMap& resolved, std::string& error);
    [[nodiscard]] bool EvaluateSoundGraph(const SoundGraphProgram& program, const ParameterMap& parameters,
        std::uint64_t& randomState, std::vector<GraphVoice>& voices, std::string& error);
}
