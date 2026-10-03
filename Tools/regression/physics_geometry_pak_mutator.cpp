#include "../../Engine/Utility_Framework/Paklib.hpp"
#include "../../Engine/RenderEngine/Experiment/Cooked/CookedAssetManifest.h"
#include <iostream>
#include <map>

int main(int argc, char** argv)
{
    namespace ck = experiment::cooked;
    try
    {
        if (argc != 5 || std::filesystem::exists(argv[2]))
            return 2;
        const std::string mode = argv[3];
        if (mode != "missing" && mode != "corrupt" && mode != "revision")
            return 2;

        Pak::Archive source(argv[1]);
        std::map<std::string, std::vector<std::byte>> files;
        for (const auto& entry : source.list())
        {
            const auto bytes = source.readAll(entry.path);
            const auto span = std::as_bytes(std::span(bytes));
            files.emplace(entry.path, std::vector<std::byte>(span.begin(), span.end()));
        }

        const std::string manifestPath = "Assets/Derived/asset-manifest.cemf";
        ck::CookedAssetManifest manifest;
        std::vector<ck::AssetManifestIssue> issues;
        if (!ck::ReadAssetManifest(files.at(manifestPath), manifest, issues))
            throw std::runtime_error("Input CEMF invalid");

        auto target = std::ranges::find_if(manifest.entries, [&](const auto& entry) {
            return Uuid::ToString(entry.assetId.value) == argv[4] &&
                   entry.kind == ck::CookedAssetKind::CollisionGeometry;
        });
        if (target == manifest.entries.end())
            throw std::runtime_error("Target geometry absent from CEMF");
        const auto path = "Assets/" + target->artifactPath;
        auto& bytes = files.at(path);
        if (bytes.size() < 68)
            throw std::runtime_error("Input CEPG too small");

        if (mode == "missing")
            files.erase(path);
        else if (mode == "corrupt")
            bytes[60] ^= std::byte{1}; // Preserve CEMF hash: test artifact integrity rejection.
        else
        {
            // A valid CEPG/CEMF contains revision2 while the cooked Scene requests revision1.
            for (unsigned index = 0; index < 8; ++index)
                bytes[36 + index] = std::byte(index == 0 ? 2 : 0);
            std::uint64_t checksum = 14695981039346656037ull;
            for (auto byte : std::span(bytes).first(bytes.size() - 8))
                checksum = (checksum ^ std::to_integer<unsigned>(byte)) * 1099511628211ull;
            for (unsigned index = 0; index < 8; ++index)
                bytes[bytes.size() - 8 + index] = std::byte((checksum >> (index * 8)) & 255);

            std::string failure;
            if (!ck::ComputeSha256(bytes, target->contentSha256, failure))
                throw std::runtime_error(failure);
            auto encoded = ck::WriteAssetManifest(manifest);
            if (!encoded.Succeeded())
                throw std::runtime_error("Mutated CEMF invalid");
            files[manifestPath] = std::move(encoded.bytes);
        }

        Pak::Builder output(argv[2]);
        for (const auto& [name, bytes] : files)
            output.addMemory(name, bytes);
        output.finish();
        std::cout << "PHYSICS_GEOMETRY_PAK_MUTATED " << mode << '\n';
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
