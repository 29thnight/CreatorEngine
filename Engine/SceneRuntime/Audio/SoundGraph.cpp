#include "SoundGraph.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <unordered_set>

namespace wave
{
    namespace
    {
        constexpr std::size_t kMaximumNodes = 1024u;
        constexpr std::uint32_t kMaximumVoices = 256u;

        bool IsFinite(const ParameterValue& value)
        {
            const float* number = std::get_if<float>(&value);
            return number == nullptr || std::isfinite(*number);
        }

        float Number(const ParameterMap& parameters, const std::string& name)
        {
            if (name.empty())
            {
                return 1.0f;
            }
            const auto found = parameters.find(name);
            if (found == parameters.end())
            {
                return 1.0f;
            }
            if (const auto value = std::get_if<float>(&found->second))
            {
                return *value;
            }
            if (const auto value = std::get_if<std::int32_t>(&found->second))
            {
                return static_cast<float>(*value);
            }
            return 1.0f;
        }

        std::uint64_t NextRandom(std::uint64_t& state)
        {
            if (state == 0u)
            {
                state = 0x9e3779b97f4a7c15ull;
            }
            state ^= state >> 12u;
            state ^= state << 25u;
            state ^= state >> 27u;
            return state * 2685821657736338717ull;
        }

        std::uint64_t BranchKey(std::uint64_t parent, std::uint32_t node, std::size_t input)
        {
            return (parent ^ (static_cast<std::uint64_t>(node) << 32u) ^ input) * 1099511628211ull;
        }
    }

    ParameterType TypeOfParameter(const ParameterValue& value) noexcept
    {
        return static_cast<ParameterType>(value.index());
    }

    std::shared_ptr<const SoundGraphProgram> CompileSoundGraph(const SoundGraphDefinition& definition,
        const std::function<bool(const ClipKey&)>& hasClip, std::string& error)
    {
        error.clear();
        const auto fail = [&error](std::string reason) -> std::shared_ptr<const SoundGraphProgram>
        {
            error = std::move(reason);
            return {};
        };
        if (definition.schemaVersion != 1u || definition.nodes.empty()
            || definition.nodes.size() > kMaximumNodes || definition.parameters.size() > 256u || definition.maximumVoices == 0u
            || definition.maximumVoices > kMaximumVoices)
        {
            return fail("SoundGraph schema, node count or maximumVoices is invalid");
        }
        std::unordered_map<std::string, ParameterDefinition> parameters;
        for (const auto& parameter : definition.parameters)
        {
            if (parameter.name.empty() || parameter.name.size() > 128u
                || TypeOfParameter(parameter.defaultValue) != parameter.type || !IsFinite(parameter.defaultValue)
                || !parameters.emplace(parameter.name, parameter).second)
            {
                return fail("SoundGraph parameter name, type or default is invalid: " + parameter.name);
            }
        }
        auto program = std::make_shared<SoundGraphProgram>();
        program->definition = definition;
        std::size_t outputs = 0u;
        for (std::size_t index = 0u; index < definition.nodes.size(); ++index)
        {
            const auto& node = definition.nodes[index];
            if (node.id == 0u || !program->nodeIndices.emplace(node.id, index).second)
            {
                return fail("SoundGraph node IDs must be unique and nonzero");
            }
            if (node.kind == SoundNodeKind::Output)
            {
                ++outputs;
            }
        }
        const auto output = program->nodeIndices.find(definition.output);
        if (outputs != 1u || output == program->nodeIndices.end()
            || definition.nodes[output->second].kind != SoundNodeKind::Output)
        {
            return fail("SoundGraph must name exactly one Output node");
        }
        const auto numericParameter = [&parameters](const std::string& name)
        {
            if (name.empty())
            {
                return true;
            }
            const auto found = parameters.find(name);
            return found != parameters.end()
                && (found->second.type == ParameterType::Float || found->second.type == ParameterType::Integer);
        };
        for (const auto& node : definition.nodes)
        {
            if (node.inputs.size() > kMaximumVoices)
            {
                return fail("SoundGraph node has too many inputs");
            }
            for (const auto input : node.inputs)
            {
                const auto found = program->nodeIndices.find(input);
                if (found == program->nodeIndices.end()
                    || definition.nodes[found->second].kind == SoundNodeKind::Parameter
                    || definition.nodes[found->second].kind == SoundNodeKind::Output)
                {
                    return fail("SoundGraph audio input is missing or has a non-audio pin type");
                }
            }
            switch (node.kind)
            {
            case SoundNodeKind::Clip:
                if (!node.inputs.empty() || !node.clip.IsGuid() || !hasClip || !hasClip(node.clip))
                {
                    return fail("SoundGraph Clip requires a loaded GUID clip: " + node.clip.Text());
                }
                break;
            case SoundNodeKind::Random:
                if (node.inputs.empty() || (!node.weights.empty() && node.weights.size() != node.inputs.size()))
                {
                    return fail("SoundGraph Random requires inputs with matching weights");
                }
                if (!node.weights.empty())
                {
                    double total = 0.0;
                    for (const auto weight : node.weights)
                    {
                        if (!std::isfinite(weight) || weight < 0.0f)
                        {
                            return fail("SoundGraph Random weights must be finite and nonnegative");
                        }
                        total += weight;
                    }
                    if (total <= 0.0)
                    {
                        return fail("SoundGraph Random requires a positive weight");
                    }
                }
                break;
            case SoundNodeKind::Switch:
            {
                const auto parameter = parameters.find(node.parameter);
                if (parameter == parameters.end() || node.inputs.empty()
                    || (node.inputs.size() != node.cases.size() && node.inputs.size() != node.cases.size() + 1u))
                {
                    return fail("SoundGraph Switch requires a parameter, cases and optional default input");
                }
                for (std::size_t index = 0u; index < node.cases.size(); ++index)
                {
                    if (TypeOfParameter(node.cases[index]) != parameter->second.type || !IsFinite(node.cases[index]))
                    {
                        return fail("SoundGraph Switch case has the wrong parameter type");
                    }
                    for (std::size_t previous = 0u; previous < index; ++previous)
                    {
                        if (node.cases[previous] == node.cases[index])
                        {
                            return fail("SoundGraph Switch contains a duplicate case");
                        }
                    }
                }
                break;
            }
            case SoundNodeKind::Layer:
                if (node.inputs.empty())
                {
                    return fail("SoundGraph Layer requires at least one input");
                }
                break;
            case SoundNodeKind::GainPitch:
                if (node.inputs.size() != 1u || !std::isfinite(node.gain) || node.gain < 0.0f
                    || !std::isfinite(node.pitch) || node.pitch <= 0.0f
                    || !numericParameter(node.gainParameter) || !numericParameter(node.pitchParameter))
                {
                    return fail("SoundGraph GainPitch requires one audio input and valid numeric parameters");
                }
                break;
            case SoundNodeKind::Parameter:
                if (!node.inputs.empty() || parameters.find(node.parameter) == parameters.end())
                {
                    return fail("SoundGraph Parameter must reference a declared typed parameter");
                }
                break;
            case SoundNodeKind::Output:
                if (node.inputs.size() != 1u)
                {
                    return fail("SoundGraph Output requires exactly one audio input");
                }
                break;
            default:
                return fail("SoundGraph contains an unknown node kind");
            }
        }
        std::vector<std::uint8_t> marks(definition.nodes.size());
        std::vector<std::uint32_t> counts(definition.nodes.size());
        std::function<bool(std::size_t)> visit = [&](std::size_t index)
        {
            if (marks[index] == 1u)
            {
                error = "SoundGraph contains a cycle";
                return false;
            }
            if (marks[index] == 2u)
            {
                return true;
            }
            marks[index] = 1u;
            const auto& node = definition.nodes[index];
            std::uint32_t count = node.kind == SoundNodeKind::Clip ? 1u : 0u;
            for (const auto input : node.inputs)
            {
                const auto child = program->nodeIndices.at(input);
                if (!visit(child))
                {
                    return false;
                }
                count = node.kind == SoundNodeKind::Layer ? count + counts[child] : std::max(count, counts[child]);
                if (count > definition.maximumVoices)
                {
                    error = "SoundGraph exceeds maximumVoices";
                    return false;
                }
            }
            counts[index] = count;
            marks[index] = 2u;
            return true;
        };
        // Validate disconnected content too: reconnecting an old bad branch
        // must never turn a previously accepted artifact into a runtime cycle.
        for (std::size_t index = 0u; index < definition.nodes.size(); ++index)
        {
            if (!visit(index))
            {
                return {};
            }
        }
        program->maximumVoices = counts[output->second];
        return program;
    }

    bool ResolveGraphParameters(const SoundGraphProgram& program, const ParameterMap& supplied,
        ParameterMap& resolved, std::string& error)
    {
        resolved.clear();
        error.clear();
        for (const auto& parameter : program.definition.parameters)
        {
            resolved.emplace(parameter.name, parameter.defaultValue);
        }
        for (const auto& [name, value] : supplied)
        {
            const auto found = resolved.find(name);
            if (found == resolved.end() || found->second.index() != value.index() || !IsFinite(value))
            {
                error = "SoundGraph parameter is unknown, nonfinite or has the wrong type: " + name;
                resolved.clear();
                return false;
            }
            found->second = value;
        }
        return true;
    }

    bool EvaluateSoundGraph(const SoundGraphProgram& program, const ParameterMap& parameters,
        std::uint64_t& randomState, std::vector<GraphVoice>& voices, std::string& error)
    {
        voices.clear();
        error.clear();
        ParameterMap resolved;
        if (!ResolveGraphParameters(program, parameters, resolved, error))
        {
            return false;
        }
        std::function<bool(std::uint32_t, float, float, std::uint64_t)> evaluate;
        evaluate = [&](std::uint32_t id, float gain, float pitch, std::uint64_t branch)
        {
            const auto& node = program.definition.nodes[program.nodeIndices.at(id)];
            const auto child = [&](std::size_t index, float childGain, float childPitch)
            {
                return evaluate(node.inputs[index], childGain, childPitch, BranchKey(branch, id, index));
            };
            switch (node.kind)
            {
            case SoundNodeKind::Clip:
                if (voices.size() >= program.maximumVoices)
                {
                    error = "SoundGraph exceeded its compiled voice bound";
                    return false;
                }
                voices.push_back({ node.clip, gain, pitch, BranchKey(branch, id, 0u) });
                return true;
            case SoundNodeKind::Random:
            {
                double total = node.weights.empty() ? static_cast<double>(node.inputs.size()) : 0.0;
                for (const auto weight : node.weights)
                {
                    total += weight;
                }
                const double unit = static_cast<double>(NextRandom(randomState) >> 11u) * (1.0 / 9007199254740992.0);
                double selected = unit * total;
                for (std::size_t index = 0u; index < node.inputs.size(); ++index)
                {
                    selected -= node.weights.empty() ? 1.0 : node.weights[index];
                    if (selected < 0.0 || index + 1u == node.inputs.size())
                    {
                        return child(index, gain, pitch);
                    }
                }
                return false;
            }
            case SoundNodeKind::Switch:
            {
                const auto& value = resolved.at(node.parameter);
                for (std::size_t index = 0u; index < node.cases.size(); ++index)
                {
                    if (node.cases[index] == value)
                    {
                        return child(index, gain, pitch);
                    }
                }
                return node.inputs.size() > node.cases.size() ? child(node.cases.size(), gain, pitch) : true;
            }
            case SoundNodeKind::Layer:
                for (std::size_t index = 0u; index < node.inputs.size(); ++index)
                {
                    if (!child(index, gain, pitch))
                    {
                        return false;
                    }
                }
                return true;
            case SoundNodeKind::GainPitch:
            {
                const float nextGain = gain * node.gain * Number(resolved, node.gainParameter);
                const float nextPitch = pitch * node.pitch * Number(resolved, node.pitchParameter);
                if (!std::isfinite(nextGain) || nextGain < 0.0f || !std::isfinite(nextPitch) || nextPitch <= 0.0f)
                {
                    error = "SoundGraph evaluated a nonfinite/negative gain or nonpositive pitch";
                    return false;
                }
                return child(0u, nextGain, nextPitch);
            }
            case SoundNodeKind::Output:
                return child(0u, gain, pitch);
            case SoundNodeKind::Parameter:
                return true;
            default:
                return false;
            }
        };
        if (!evaluate(program.definition.output, 1.0f, 1.0f, 14695981039346656037ull))
        {
            voices.clear();
            return false;
        }
        return true;
    }
}
