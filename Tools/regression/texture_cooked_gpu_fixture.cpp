#include <d3d11.h>
#include <DirectXTex.h>
#include "Experiment/Cooked/CookedTexture.h"
#include "../../Engine/Utility_Framework/StandardLibrary.h"

namespace
{
    void Check(bool value, const std::string& message)
    {
        if (!value)
        {
            throw std::runtime_error(message);
        }
    }

    void Write(const std::filesystem::path& path, const void* bytes, size_t size)
    {
        std::ofstream stream(path, std::ios::binary);
        stream.write(static_cast<const char*>(bytes), size);
        Check(!!stream, "Fixture write failed");
    }
}

int main(int argc, char** argv)
{
    try
    {
        Check(argc == 2, "output directory required");
        const std::filesystem::path root = argv[1];
        std::filesystem::create_directories(root);
        DirectX::ScratchImage source;
        Check(SUCCEEDED(source.Initialize2D(DXGI_FORMAT_R8G8B8A8_UNORM, 8, 8, 1, 1)), "Source allocation");
        const auto* input = source.GetImage(0, 0, 0);
        for (unsigned y = 0; y < 8; ++y)
        {
            for (unsigned x = 0; x < 8; ++x)
            {
                auto* pixel = input->pixels + y * input->rowPitch + x * 4;
                pixel[0] = static_cast<uint8_t>(24 + x * 27);
                pixel[1] = static_cast<uint8_t>(32 + y * 25);
                pixel[2] = static_cast<uint8_t>((x ^ y) * 31);
                pixel[3] = static_cast<uint8_t>(64 + ((x + y) % 8) * 27);
            }
        }
        DirectX::Blob dds;
        Check(SUCCEEDED(DirectX::SaveToDDSMemory(*input, DirectX::DDS_FLAGS_NONE, dds)), "DDS fixture encoding");
        const std::span<const std::byte> bytes{ reinterpret_cast<const std::byte*>(dds.GetBufferPointer()), dds.GetBufferSize() };
        for (const std::string name : { "bc5", "bc7", "bc7-srgb" })
        {
            namespace cooked = experiment::cooked;
            cooked::TextureImportSettings settings;
            settings.compression = name == "bc5" ? cooked::TextureCompression::BC5 : cooked::TextureCompression::BC7;
            settings.colorSpace = name == "bc7-srgb" ? cooked::TextureColorSpace::Srgb : cooked::TextureColorSpace::Linear;
            settings.mipPolicy = cooked::TextureMipPolicy::GenerateFull;
            settings.normalMap = name == "bc5";
            std::vector<std::byte> artifact;
            std::string error;
            Check(cooked::CookTexture(bytes, settings, artifact, error), error);
            Write(root / (name + ".cetex"), artifact.data(), artifact.size());
            TextureImage decoded;
            Check(cooked::DecodeCookedTexture(artifact, decoded, error), error);
            Check(decoded.View().MipLevels() == 4, "Four cooked mip levels required");
            std::vector<float> reference;
            for (unsigned mip = 0; mip < 4; ++mip)
            {
                const auto* sub = decoded.Find(mip, 0);
                Check(sub != nullptr, "Cooked mip missing");
                DirectX::Image block{ sub->width, sub->height,
                    name == "bc5" ? DXGI_FORMAT_BC5_UNORM : DXGI_FORMAT_BC7_UNORM,
                    sub->rowPitch, sub->slicePitch,
                    const_cast<uint8_t*>(reinterpret_cast<const uint8_t*>(sub->pixels)) };
                DirectX::ScratchImage floats;
                Check(SUCCEEDED(DirectX::Decompress(block, DXGI_FORMAT_R32G32B32A32_FLOAT, floats)), "CPU block reference decode");
                const auto* image = floats.GetImage(0, 0, 0);
                for (unsigned y = 0; y < sub->height; ++y)
                {
                    const auto* row = reinterpret_cast<const float*>(image->pixels + y * image->rowPitch);
                    for (unsigned x = 0; x < sub->width * 4; ++x)
                    {
                        auto value = row[x];
                        if (name == "bc7-srgb" && x % 4 != 3)
                        {
                            value = value <= 0.04045f ? value / 12.92f : std::pow((value + 0.055f) / 1.055f, 2.4f);
                        }
                        reference.push_back(value);
                    }
                }
            }
            Write(root / (name + ".reference"), reference.data(), reference.size() * sizeof(float));
            std::cout << "FIXTURE_OK format=" << name << " mips=4 components=" << reference.size() << '\n';
        }
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
