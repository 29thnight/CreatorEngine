#include "../../Editor/EngineEntry/Assets/Textures/TextureGenerationHash.h"

std::array<std::uint8_t, 32> text_cng_reference(std::span<const std::uint8_t> bytes)
{
    TextureGenerationHash reference;
    reference.Update(bytes.data(), bytes.size());
    std::array<std::uint8_t, 32> expected{};
    std::string failure;
    if (!reference.Finish(expected, failure))
    {
        throw std::runtime_error(failure);
    }
    return expected;
}
