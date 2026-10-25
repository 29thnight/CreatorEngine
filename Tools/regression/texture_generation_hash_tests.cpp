#include "../../Editor/EngineEntry/Assets/Textures/TextureGenerationHash.h"
#if defined(CE_CONTRACT_STANDALONE)
#include "../build/ModuleAdapters/Creator.Core.Sha256.h"
#else
import Creator.Core.Sha256;
#endif

int main()
{
    unsigned checks = 0;
    const auto check = [&](bool value) {
        ++checks;
        if (!value) throw std::runtime_error("Texture generation digest regression");
    };
    const auto digest = [&](std::span<const std::uint8_t> bytes, std::size_t chunk) {
        TextureGenerationHash hash;
        hash.Update(nullptr, 0);
        for (std::size_t offset = 0; offset < bytes.size(); offset += chunk)
            hash.Update(bytes.data() + offset, (std::min)(chunk, bytes.size() - offset));
        ce::hash::sha256_digest output{};
        std::string failure;
        check(hash.Finish(output, failure) && failure.empty());
        check(output == ce::hash::sha256::compute(bytes.data(), bytes.size()));
        return output;
    };
    const std::vector<std::uint8_t> empty;
    check(ce::hash::to_hex(digest(empty, 1)) ==
        "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
    const std::array<std::uint8_t, 3> abc{ 'a', 'b', 'c' };
    check(ce::hash::to_hex(digest(abc, 1)) ==
        "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    for (const std::size_t size : { 55u, 56u, 63u, 64u, 65u, 65535u, 65536u, 65537u, 1000000u })
    {
        std::vector<std::uint8_t> bytes(size);
        for (std::size_t i = 0; i < size; ++i) bytes[i] = static_cast<std::uint8_t>(i * 17u);
        const auto original = digest(bytes, 65536);
        check(digest(bytes, 13) == original);
        bytes[size / 2] ^= 1;
        check(digest(bytes, 65536) != original);
    }
    // An invalid CNG state must not return a digest that callers could accept.
    TextureGenerationHash finished;
    ce::hash::sha256_digest output{};
    std::string failure;
    check(finished.Finish(output, failure));
    finished.Update(abc.data(), abc.size());
    output.fill(0x5a);
    const auto sentinel = output;
    check(!finished.Finish(output, failure) && !failure.empty() && output == sentinel);
    std::printf("TEXTURE_GENERATION_HASH_OK checks=%u\n", checks);
}
