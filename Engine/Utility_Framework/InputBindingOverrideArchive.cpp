#include "InputBindingOverrideArchive.h"

#include <bit>
#include <stdexcept>

namespace Input
{
    namespace OverrideWire
    {
        constexpr std::uint32_t kMagic = 0x4f494543u; // CEIO
        constexpr std::size_t kMaxBindings = 4096u;
        constexpr std::size_t kMaxSources = 64u;

        void Append(std::vector<std::byte>& bytes, std::uint64_t value, unsigned count)
        {
            for (unsigned index = 0; index < count; ++index)
            {
                bytes.push_back(static_cast<std::byte>((value >> (index * 8u)) & 0xffu));
            }
        }

        class Reader final
        {
        public:
            explicit Reader(std::span<const std::byte> bytes) : m_bytes(bytes) {}
            std::uint64_t Number(unsigned count)
            {
                if (count > m_bytes.size() - m_offset)
                {
                    throw std::runtime_error("Input override archive is truncated.");
                }
                std::uint64_t result{};
                for (unsigned index = 0; index < count; ++index)
                {
                    result |= std::uint64_t(std::to_integer<std::uint8_t>(m_bytes[m_offset++])) << (index * 8u);
                }
                return result;
            }
            float Float() { return std::bit_cast<float>(static_cast<std::uint32_t>(Number(4))); }
            bool AtEnd() const { return m_offset == m_bytes.size(); }
        private:
            std::span<const std::byte> m_bytes;
            std::size_t m_offset{};
        };

        bool Validate(const InputGraphProgram& program, std::span<const InputBindingOverride> overrides,
            std::string& failure)
        {
            std::vector<InputDiagnostic> diagnostics;
            if (!ApplyInputOverrides(program, overrides, diagnostics))
            {
                failure = diagnostics.empty() ? "Input overrides are incompatible with this graph." : diagnostics.front().message;
                return false;
            }
            return true;
        }
    }

    bool WriteInputBindingOverrides(UserID user, const InputGraphProgram& program,
        std::span<const InputBindingOverride> overrides, std::vector<std::byte>& bytes, std::string& failure)
    {
        try
        {
            if (overrides.size() > OverrideWire::kMaxBindings || !OverrideWire::Validate(program, overrides, failure))
            {
                if (overrides.size() > OverrideWire::kMaxBindings)
                {
                    failure = "Too many persisted input overrides.";
                }
                return false;
            }
            std::vector<std::byte> candidate;
            const auto append = [&](std::uint64_t value, unsigned count) { OverrideWire::Append(candidate, value, count); };
            append(OverrideWire::kMagic, 4);
            append(kInputOverrideArchiveVersion, 4);
            append(kInputSchemaVersion, 4);
            append(kInputCompilerVersion, 4);
            append(kInputABIVersion, 4);
            append(program.GetDefinition().id.high, 8);
            append(program.GetDefinition().id.low, 8);
            append(user, 8);
            append(overrides.size(), 4);
            for (const auto& binding : overrides)
            {
                if (binding.sources.empty() || binding.sources.size() > OverrideWire::kMaxSources)
                {
                    failure = "Input override source count exceeds the format bound.";
                    return false;
                }
                append(binding.binding.high, 8);
                append(binding.binding.low, 8);
                append(binding.sources.size(), 4);
                for (const auto& source : binding.sources)
                {
                    append(static_cast<std::uint8_t>(source.control.kind), 1);
                    append(source.control.code, 4);
                    append(std::bit_cast<std::uint32_t>(source.scaleX), 4);
                    append(std::bit_cast<std::uint32_t>(source.scaleY), 4);
                    append(static_cast<std::uint8_t>(source.space), 1);
                }
            }
            if (candidate.size() > kInputOverrideMaxBytes)
            {
                failure = "Input override archive exceeds its byte limit.";
                return false;
            }
            bytes = std::move(candidate);
            failure.clear();
            return true;
        }
        catch (const std::exception& error)
        {
            failure = error.what();
            return false;
        }
    }

    bool ReadInputBindingOverrides(std::span<const std::byte> bytes, UserID expectedUser,
        const InputGraphProgram& program, std::vector<InputBindingOverride>& overrides, std::string& failure)
    {
        try
        {
            if (bytes.size() > kInputOverrideMaxBytes)
            {
                throw std::runtime_error("Input override archive exceeds its byte limit.");
            }
            OverrideWire::Reader reader(bytes);
            if (reader.Number(4) != OverrideWire::kMagic || reader.Number(4) != kInputOverrideArchiveVersion ||
                reader.Number(4) != kInputSchemaVersion || reader.Number(4) != kInputCompilerVersion ||
                reader.Number(4) != kInputABIVersion)
            {
                throw std::runtime_error("Unsupported input override archive/schema/compiler/API version.");
            }
            const GraphID graph{ reader.Number(8), reader.Number(8) };
            if (graph != program.GetDefinition().id || reader.Number(8) != expectedUser)
            {
                throw std::runtime_error("Input override archive belongs to a different graph or user.");
            }
            const auto count = reader.Number(4);
            if (count > OverrideWire::kMaxBindings)
            {
                throw std::runtime_error("Input override binding count exceeds its format limit.");
            }
            std::vector<InputBindingOverride> candidate;
            for (std::uint64_t index = 0; index < count; ++index)
            {
                InputBindingOverride binding;
                binding.graph = graph;
                binding.binding = { reader.Number(8), reader.Number(8) };
                const auto sources = reader.Number(4);
                if (sources == 0u || sources > OverrideWire::kMaxSources)
                {
                    throw std::runtime_error("Input override source count is invalid.");
                }
                for (std::uint64_t sourceIndex = 0; sourceIndex < sources; ++sourceIndex)
                {
                    InputSource source;
                    source.control.kind = static_cast<SourceKind>(reader.Number(1));
                    source.control.code = static_cast<std::uint32_t>(reader.Number(4));
                    source.scaleX = reader.Float();
                    source.scaleY = reader.Float();
                    source.space = static_cast<CoordinateSpace>(reader.Number(1));
                    binding.sources.push_back(source);
                }
                candidate.push_back(std::move(binding));
            }
            if (!reader.AtEnd())
            {
                throw std::runtime_error("Input override archive has trailing bytes.");
            }
            if (!OverrideWire::Validate(program, candidate, failure))
            {
                return false;
            }
            overrides = std::move(candidate);
            failure.clear();
            return true;
        }
        catch (const std::exception& error)
        {
            failure = error.what();
            return false;
        }
    }
}
