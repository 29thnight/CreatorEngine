#include "GltfSourceDependencies.h"

#include <fastgltf/core.hpp>
#include <fastgltf/types.hpp>

#include <algorithm>
#include <cctype>
#include <string_view>
#include <system_error>
#include <variant>

namespace experiment::importer
{
    namespace
    {
        namespace fs = std::filesystem;

        [[nodiscard]] std::string ToLowerExtension(const fs::path& path)
        {
            std::string extension = path.extension().string();
            std::ranges::transform(extension, extension.begin(),
                [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            return extension;
        }

        // 원본 폴더 안에 머무는 상대 경로만 통과시킨다.
        //
        // `is_absolute()` 하나로는 부족하다 — Windows 에서 "/etc/passwd" 는 root
        // name 이 없어 절대 경로가 아니라고 나오고, "C:Textures" 는 root name 만
        // 있는 드라이브 상대 경로다. 둘 다 원본 폴더 밖을 가리킬 수 있다.
        [[nodiscard]] bool IsContainedRelative(const fs::path& candidate)
        {
            if (candidate.empty()) return false;
            if (candidate.is_absolute()) return false;
            if (candidate.has_root_name() || candidate.has_root_directory()) return false;

            const fs::path normalized = candidate.lexically_normal();
            if (normalized.empty() || normalized == ".") return false;
            for (const fs::path& part : normalized)
            {
                if (part == "..") return false;
            }
            return true;
        }
    }

    const char* ToString(const GltfDependencyRejection rejection) noexcept
    {
        switch (rejection)
        {
        case GltfDependencyRejection::None:           return "none";
        case GltfDependencyRejection::ParseFailed:    return "parse_failed";
        case GltfDependencyRejection::NonLocalUri:    return "non_local_uri";
        case GltfDependencyRejection::EscapingUri:    return "escaping_uri";
        case GltfDependencyRejection::MissingSidecar: return "missing_sidecar";
        }
        return "unknown";
    }

    GltfSourceDependencies ScanGltfSourceDependencies(const fs::path& sourcePath)
    {
        GltfSourceDependencies result;

        const std::string extension = ToLowerExtension(sourcePath);
        if (extension != ".gltf" && extension != ".glb") return result;

        auto data = fastgltf::GltfDataBuffer::FromPath(sourcePath);
        if (data.error() != fastgltf::Error::None)
        {
            result.rejection = GltfDependencyRejection::ParseFailed;
            result.rejectionDetail =
                std::string(fastgltf::getErrorMessage(data.error()));
            return result;
        }

        const fs::path baseDirectory = sourcePath.parent_path();

        // Options::None 이 핵심이다. LoadExternalBuffers/Images 를 주면 fastgltf 가
        // 파일을 직접 열어 sources::Array 로 바꿔 버려서 "어떤 파일을 참조하는가"가
        // 지워진다. 여기서는 URI 를 URI 인 채로 받아야 한다. data: URI 만은
        // 파서가 항상 먼저 디코드하므로 목록에 들어오지 않는다.
        //
        // ★ 확장 집합은 GltfImporter 와 **같아야** 한다. extensionsRequired 에
        //   실린 확장을 파서가 모르면 fastgltf 는 MissingExtensions 로 끊는다 —
        //   여기가 임포터보다 엄격하면 지금까지 열리던 모델이 훑기 단계에서
        //   거부된다. 둘이 어긋나지 않는지는 게이트가 소스 대조로 지킨다
        //   (verify-model-multifile-import.ps1).
        fastgltf::Parser parser(fastgltf::Extensions::KHR_texture_transform
            | fastgltf::Extensions::KHR_materials_emissive_strength);
        auto loaded = parser.loadGltf(data.get(), baseDirectory,
            fastgltf::Options::None);
        if (loaded.error() != fastgltf::Error::None)
        {
            result.rejection = GltfDependencyRejection::ParseFailed;
            result.rejectionDetail =
                std::string(fastgltf::getErrorMessage(loaded.error()));
            return result;
        }

        const fastgltf::Asset& asset = loaded.get();

        std::vector<std::string> seen;
        const auto consider = [&](const fastgltf::DataSource& source,
            const std::string_view category) -> bool
        {
            const auto* const uriSource =
                std::get_if<fastgltf::sources::URI>(&source);
            if (nullptr == uriSource) return true;

            const fastgltf::URI& uri = uriSource->uri;
            if (uri.isDataUri()) return true;

            // fastgltf::URI 는 퍼센트 인코딩을 이미 풀어 두었다. `%2e%2e%2f` 같은
            // 위장 탈출이 여기서 평범한 `../` 로 보이는 이유다.
            const std::string text(uri.string());
            const auto reject = [&](const GltfDependencyRejection rejection)
            {
                result.rejection = rejection;
                result.rejectionDetail =
                    std::string(category) + " uri=" + text;
                result.relativePaths.clear();
                return false;
            };

            if (!uri.valid() || !uri.isLocalPath())
                return reject(GltfDependencyRejection::NonLocalUri);

            const fs::path relative = uri.fspath();
            if (!IsContainedRelative(relative))
                return reject(GltfDependencyRejection::EscapingUri);

            const fs::path normalized = relative.lexically_normal();
            std::error_code error;
            if (!fs::is_regular_file(baseDirectory / normalized, error) || error)
                return reject(GltfDependencyRejection::MissingSidecar);

            std::string key = normalized.generic_string();
            if (std::ranges::find(seen, key) == seen.end())
            {
                seen.push_back(std::move(key));
                result.relativePaths.push_back(normalized);
            }
            return true;
        };

        for (const fastgltf::Buffer& buffer : asset.buffers)
        {
            if (!consider(buffer.data, "buffers")) return result;
        }
        for (const fastgltf::Image& image : asset.images)
        {
            if (!consider(image.data, "images")) return result;
        }
        return result;
    }
}
