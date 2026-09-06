#include "ExperimentParity/ExperimentMaterialResolveSelfTest.h"

#include "DataSystem.h"
#include "Experiment/AssetIdentity.h"
#include "Experiment/MaterialResolver.h"
#include "ExperimentMaterialResolveBinding.h"
#include "RHI/RHIShaderSource.h"
#include "ShaderMeta.h"
#include "Texture.h"

#include <cstdio>
#include <filesystem>
#include <array>
#include <fstream>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

namespace RenderTest
{
    namespace
    {
        struct Checker final
        {
            std::string& log;
            std::size_t passed{};
            std::size_t failed{};

            void Check(bool condition, const std::string& what)
            {
                if (condition) { ++passed; return; }
                ++failed;
                log += "    [실패] " + what + "\n";
            }
        };

        [[nodiscard]] experiment::AssetId MakeAssetId(const char* text)
        {
            experiment::AssetId id{};
            (void)experiment::TryParseCanonicalAssetId(text, id);
            return id;
        }

        // 축 3개 — SHADOW의 "high"가 QUALITY와 겹쳐 모호 케이스를 데이터로 갖는다.
        [[nodiscard]] std::shared_ptr<const ShaderMeta> MakeSyntheticMeta(
            const FileGuid& guid)
        {
            auto meta = std::make_shared<ShaderMeta>();
            meta->guid = guid;
            meta->name = "ResolveProbe";
            meta->keywords = {
                { "QUALITY", { "low", "high" } },
                { "FOG", { "off", "on" } },
                { "SHADOW", { "off", "high" } },
            };
            return meta;
        }

        struct FakeServices final
        {
            experiment::MaterialResolveServices services{};
            ShaderMetaHandle handle{ 7, 3 };
            std::shared_ptr<const ShaderMeta> meta{};
            FileGuid lastShaderGuid{};

            std::size_t shaderLoads{};
            std::size_t cookedLookups{};
            std::size_t sourceLookups{};
            std::vector<std::filesystem::path> loadedPaths{};
            std::vector<bool> loadedCompress{};
            std::vector<experiment::TextureColorSpace> loadedColorSpaces{};

            // cooked 표: 여기 있는 GUID만 cooked 로 해석된다.
            std::vector<experiment::AssetId> cookedIds{};
            bool textureLoadFails{};

            void Wire(const FileGuid& shaderGuid)
            {
                meta = MakeSyntheticMeta(shaderGuid);
                services.loadShaderMetaHandle =
                    [this](const FileGuid& guid, std::string&)
                    {
                        ++shaderLoads;
                        lastShaderGuid = guid;
                        return handle;
                    };
                services.resolveShaderMeta = [this](const ShaderMetaHandle&)
                    {
                        return meta;
                    };
                services.resolveCookedArtifactPath =
                    [this](const experiment::AssetId& id)
                    {
                        ++cookedLookups;
                        for (const experiment::AssetId& cooked : cookedIds)
                        {
                            if (cooked == id)
                            {
                                return std::filesystem::path("Derived/Textures/")
                                    / (Uuid::ToString(id.value) + ".png");
                            }
                        }
                        return std::filesystem::path{};
                    };
                services.resolveSourcePath = [this](const FileGuid& guid)
                    {
                        ++sourceLookups;
                        return std::filesystem::path("Assets/Textures/")
                            / (guid.ToString() + ".png");
                    };
                services.loadTexture =
                    [this](const std::filesystem::path& path, bool compress, experiment::TextureColorSpace colorSpace)
                        -> std::shared_ptr<Texture>
                    {
                        loadedPaths.push_back(path);
                        loadedCompress.push_back(compress);
                        loadedColorSpaces.push_back(colorSpace);
                        if (textureLoadFails) return nullptr;
                        return std::make_shared<Texture>();
                    };
            }
        };

        void CheckRejected(Checker& check,
            const experiment::Material& material,
            const experiment::MaterialResolveServices& services,
            const std::string& what)
        {
            experiment::ResolvedMaterial resolved;
            std::string error;
            const bool ok = experiment::ResolveMaterial(
                material, services, resolved, error);
            check.Check(!ok, what + " — 거부해야 한다");
            check.Check(!error.empty(), what + " — 거부 사유가 있어야 한다");
        }
    }

    bool RunExperimentMaterialResolveSelfTest(std::string& outLog)
    {
        Checker check{ outLog };
        outLog += "[experiment.matresolve] 합성 검사\n";

        const experiment::AssetId shaderId =
            MakeAssetId("11111111-1111-4111-8111-111111111111");
        const experiment::AssetId materialId =
            MakeAssetId("22222222-2222-4222-8222-222222222222");
        const experiment::AssetId cookedTexture =
            MakeAssetId("33333333-3333-4333-8333-333333333333");
        const experiment::AssetId sourceTexture =
            MakeAssetId("44444444-4444-4444-8444-444444444444");
        FileGuid shaderGuid{};
        shaderGuid.m_guid = shaderId.value;

        // ── 1. 정상 해석 — cooked 우선·source 폴백·keyword 정규화 ─────────
        {
            FakeServices fake;
            fake.Wire(shaderGuid);
            fake.cookedIds = { cookedTexture };

            experiment::Material material;
            material.assetId = materialId;
            material.shaderAssetId = shaderId;
            material.name = "probe";
            material.keywordSelections = { 1 };      // QUALITY=high (인덱스, 보조)
            material.keywords = { "on" };            // FOG=on (이름, 정본)
            material.properties = {
                { "baseColorMap",
                  experiment::TextureReference{ cookedTexture, "albedo", {}, experiment::TextureColorSpace::Srgb } },
                { "ormMap",
                  experiment::TextureReference{ sourceTexture, "orm" } },
                { "emissiveMap",
                  experiment::TextureReference{ experiment::AssetId{}, "none" } },
                { "metallic", 0.5f },                // texture 아님 — 건드리지 않는다
            };

            experiment::ResolvedMaterial resolved;
            std::string error;
            const bool ok = experiment::ResolveMaterial(
                material, fake.services, resolved, error);
            check.Check(ok, "정상 해석 통과 (" + error + ")");
            if (ok)
            {
                check.Check(resolved.assetId == materialId, "assetId 보존");
                check.Check(resolved.shaderMetaHandle == fake.handle,
                    "shaderMetaHandle");
                check.Check(resolved.shaderMeta == fake.meta, "meta generation");
                check.Check(fake.lastShaderGuid == shaderGuid,
                    "shader GUID로 handle을 물어야 한다");
                check.Check(resolved.keywordSelections
                    == std::vector<std::uint16_t>{ 1, 1, 0 },
                    "keyword 정규화 — 인덱스(QUALITY=1)+이름(FOG=on)+기본(SHADOW=0)");

                check.Check(resolved.textures.size() == 2u,
                    "nil texture는 entry가 되면 안 된다");
                check.Check(resolved.notes.cookedTextures == 1u
                    && resolved.notes.sourceFallbackTextures == 1u,
                    "폴백이 관측 가능해야 한다 — cooked 1 · source 1");
                check.Check(resolved.textures.size() == 2u
                    && resolved.textures[0].fromCookedArtifact
                    && !resolved.textures[1].fromCookedArtifact,
                    "texture별 cooked/source 표식");
                check.Check(resolved.textures.size() == 2u
                    && resolved.textures[0].owner && resolved.textures[1].owner,
                    "generation owner 보존");

                // ★ 호출 계수 — cooked 로 해석된 texture 는 source 를 묻지 않는다.
                check.Check(fake.cookedLookups == 2u,
                    "cooked 조회는 유효 texture마다 한 번");
                check.Check(fake.sourceLookups == 1u,
                    "source 조회는 cooked 미해석 texture에만");
                check.Check(fake.shaderLoads == 1u, "shader 조회는 한 번");
                check.Check(fake.loadedCompress
                    == std::vector<bool>{ true, false },
                    "compress is limited to sRGB base color");
                check.Check(fake.loadedColorSpaces == std::vector{experiment::TextureColorSpace::Srgb,
                    experiment::TextureColorSpace::Linear}, "W6 source/cooked color space reaches loader");
                check.Check(fake.loadedPaths.size() == 2u
                    && fake.loadedPaths[0].generic_string().starts_with("Derived/")
                    && fake.loadedPaths[1].generic_string().starts_with("Assets/"),
                    "cooked/source 경로가 각자에게 전달돼야 한다");
            }
        }

        // ── 2. 이름이 인덱스를 덮는다 ─────────────────────────────────────
        {
            FakeServices fake;
            fake.Wire(shaderGuid);
            experiment::Material material;
            material.shaderAssetId = shaderId;
            material.keywordSelections = { 1, 1, 1 };
            material.keywords = { "low" };           // QUALITY 축을 이름으로 0으로
            experiment::ResolvedMaterial resolved;
            std::string error;
            const bool ok = experiment::ResolveMaterial(
                material, fake.services, resolved, error);
            check.Check(ok, "이름 우선 해석 통과 (" + error + ")");
            check.Check(ok && resolved.keywordSelections
                == std::vector<std::uint16_t>{ 0, 1, 1 },
                "이름 기반 keyword가 인덱스 선택을 덮어야 한다");
        }

        // ── 3. catalog 부재 — source 만으로 동작하고 폴백으로 센다 ────────
        {
            FakeServices fake;
            fake.Wire(shaderGuid);
            fake.services.resolveCookedArtifactPath = nullptr;
            experiment::Material material;
            material.shaderAssetId = shaderId;
            material.properties = {
                { "baseColorMap",
                  experiment::TextureReference{ cookedTexture, "albedo", {}, experiment::TextureColorSpace::Srgb } },
            };
            experiment::ResolvedMaterial resolved;
            std::string error;
            const bool ok = experiment::ResolveMaterial(
                material, fake.services, resolved, error);
            check.Check(ok, "catalog 부재 해석 통과 (" + error + ")");
            check.Check(ok && resolved.notes.sourceFallbackTextures == 1u
                && resolved.notes.cookedTextures == 0u
                && fake.cookedLookups == 0u,
                "catalog 부재 시 cooked 조회 없이 source로 센다");
        }

        // ── 4. fail-closed ────────────────────────────────────────────────
        {
            {
                FakeServices fake;
                fake.Wire(shaderGuid);
                fake.services.loadTexture = nullptr;
                experiment::Material material;
                material.shaderAssetId = shaderId;
                CheckRejected(check, material, fake.services, "불완전한 서비스");
            }
            {
                FakeServices fake;
                fake.Wire(shaderGuid);
                experiment::Material material;   // shaderAssetId nil
                CheckRejected(check, material, fake.services, "nil shaderAssetId");
            }
            {
                FakeServices fake;
                fake.Wire(shaderGuid);
                fake.handle = {};                // invalid handle
                experiment::Material material;
                material.shaderAssetId = shaderId;
                CheckRejected(check, material, fake.services, "handle 해석 실패");
            }
            {
                FakeServices fake;
                fake.Wire(shaderGuid);
                fake.meta = nullptr;             // 낡은 handle
                experiment::Material material;
                material.shaderAssetId = shaderId;
                CheckRejected(check, material, fake.services, "낡은 handle");
            }
            {
                FakeServices fake;
                FileGuid other{};
                other.m_guid = MakeAssetId(
                    "55555555-5555-4555-8555-555555555555").value;
                fake.Wire(other);                // meta.guid != shaderAssetId
                experiment::Material material;
                material.shaderAssetId = shaderId;
                CheckRejected(check, material, fake.services, "meta GUID 불일치");
            }
            {
                FakeServices fake;
                fake.Wire(shaderGuid);
                experiment::Material material;
                material.shaderAssetId = shaderId;
                material.keywordSelections = { 0, 0, 0, 0 };   // 축 3개 초과
                CheckRejected(check, material, fake.services, "축 수 초과 선택");
            }
            {
                FakeServices fake;
                fake.Wire(shaderGuid);
                experiment::Material material;
                material.shaderAssetId = shaderId;
                material.keywordSelections = { 2 };            // QUALITY 범위 밖
                CheckRejected(check, material, fake.services, "범위 밖 인덱스");
            }
            {
                FakeServices fake;
                fake.Wire(shaderGuid);
                experiment::Material material;
                material.shaderAssetId = shaderId;
                material.keywords = { "" };
                CheckRejected(check, material, fake.services, "빈 keyword");
            }
            {
                FakeServices fake;
                fake.Wire(shaderGuid);
                experiment::Material material;
                material.shaderAssetId = shaderId;
                material.keywords = { "ultra" };
                CheckRejected(check, material, fake.services, "미지의 keyword");
            }
            {
                FakeServices fake;
                fake.Wire(shaderGuid);
                experiment::Material material;
                material.shaderAssetId = shaderId;
                material.keywords = { "high" };    // QUALITY/SHADOW 양쪽에 있다
                CheckRejected(check, material, fake.services, "모호한 keyword");
            }
            {
                FakeServices fake;
                fake.Wire(shaderGuid);
                fake.services.resolveSourcePath = [](const FileGuid&)
                    {
                        return std::filesystem::path{};
                    };
                experiment::Material material;
                material.shaderAssetId = shaderId;
                material.properties = {
                    { "baseColorMap",
                      experiment::TextureReference{ sourceTexture, "albedo" } },
                };
                CheckRejected(check, material, fake.services,
                    "cooked/source 모두 미해석 texture");
            }
            {
                FakeServices fake;
                fake.Wire(shaderGuid);
                fake.textureLoadFails = true;
                experiment::Material material;
                material.shaderAssetId = shaderId;
                material.properties = {
                    { "baseColorMap",
                      experiment::TextureReference{ sourceTexture, "albedo" } },
                };
                CheckRejected(check, material, fake.services, "texture 로드 실패");
            }
        }

        char summary[160]{};
        std::snprintf(summary, sizeof(summary),
            "  합성 단정 %zu/%zu\n", check.passed,
            check.passed + check.failed);
        outLog += summary;
        return check.failed == 0u;
    }

    bool RunExperimentMaterialResolveReal(std::string& outLog)
    {
        Checker check{ outLog };
        outLog += "[experiment.matresolve] 실사 검사 — DataSystem 바인딩\n";

        const std::filesystem::path metaPath = RHIShaderSource::Resolve(
            "SelfTest/ShaderMetaFixture.shadermeta");
        const FileGuid guid = DataSystems->GetFileGuid(metaPath);
        if (FileGuid{} == guid)
        {
            outLog += "    [실패] ShaderMetaFixture GUID를 얻지 못했다\n";
            return false;
        }

        const experiment::MaterialResolveServices services =
            experiment::MakeDataSystemMaterialResolveServices(nullptr);
        check.Check(static_cast<bool>(services.loadShaderMetaHandle)
            && static_cast<bool>(services.resolveShaderMeta)
            && static_cast<bool>(services.loadTexture)
            && static_cast<bool>(services.resolveSourcePath),
            "제품 바인딩이 서비스 전부를 채워야 한다");
        check.Check(!services.resolveCookedArtifactPath,
            "catalog 부재면 cooked 해석은 비어야 한다");

        experiment::Material material;
        material.shaderAssetId.value = guid.m_guid;
        material.name = "RealResolveProbe";
        material.keywords = { "high" };   // fixture QUALITY 축 — 유일 매치

        experiment::ResolvedMaterial resolved;
        std::string error;
        const bool ok = experiment::ResolveMaterial(
            material, services, resolved, error);
        check.Check(ok, "실사 해석 통과 (" + error + ")");
        if (ok)
        {
            check.Check(resolved.shaderMetaHandle.IsValid(),
                "실제 DataSystem handle이 유효해야 한다");
            check.Check(nullptr != resolved.shaderMeta
                && resolved.shaderMeta->guid == guid,
                "resolve된 meta가 fixture여야 한다");
            check.Check(resolved.keywordSelections
                == std::vector<std::uint16_t>{ 1 },
                "fixture QUALITY=high 정규화");
            check.Check(resolved.textures.empty()
                && 0u == resolved.notes.cookedTextures
                && 0u == resolved.notes.sourceFallbackTextures,
                "texture 없는 material은 owner도 계수도 없어야 한다");
        }

        // Real external/cooked loader boundary: encoded bytes do not inherit
        // a prior role or collide with the same filename in another directory.
        {
            const auto directory = std::filesystem::temp_directory_path()
                / ("creator-emission-" + FileGuid::CreateRandomV4().ToString());
            std::filesystem::create_directories(directory / "a");
            std::filesystem::create_directories(directory / "b");
            const auto first = directory / "a" / "same.tga";
            const auto second = directory / "b" / "same.tga";
            std::array<unsigned char, 22> tga{};
            tga[2] = 2; tga[12] = 1; tga[14] = 1; tga[16] = 32; tga[17] = 0x28;
            tga[18] = 192; tga[19] = 64; tga[20] = 128; tga[21] = 255;
            { std::ofstream file(first, std::ios::binary); file.write(reinterpret_cast<const char*>(tga.data()), tga.size()); }
            tga[18] = 16;
            { std::ofstream file(second, std::ios::binary); file.write(reinterpret_cast<const char*>(tga.data()), tga.size()); }
            const auto linear = services.loadTexture(first, false, experiment::TextureColorSpace::Linear);
            const auto srgb = services.loadTexture(first, false, experiment::TextureColorSpace::Srgb);
            const auto other = services.loadTexture(second, false, experiment::TextureColorSpace::Srgb);
            const bool loaded = linear && srgb && other;
            check.Check(loaded, "W6 external texture loader accepts both color roles");
            if (loaded)
            {
                check.Check(linear->GetImageView().Format() == RHIFormat::RGBA8Unorm
                    || linear->GetImageView().Format() == RHIFormat::BGRA8Unorm,
                    "W6 external data texture remains linear");
                check.Check(srgb->GetImageView().Format() == RHIFormat::RGBA8UnormSrgb
                    || srgb->GetImageView().Format() == RHIFormat::BGRA8UnormSrgb,
                    "W6 external emission texture uses sRGB sampling");
                check.Check(linear->m_assetId != srgb->m_assetId
                    && std::memcmp(linear->GetImageView().At(0)->pixels, srgb->GetImageView().At(0)->pixels, 4) == 0,
                    "W6 roles have separate GPU identities and identical encoded bytes");
                check.Check(srgb == services.loadTexture(first, false, experiment::TextureColorSpace::Srgb)
                    && srgb->m_assetId != other->m_assetId
                    && std::memcmp(srgb->GetImageView().At(0)->pixels, other->GetImageView().At(0)->pixels, 4) != 0,
                    "W6 cache reuses exact path/role and separates same filenames");
            }
            std::error_code ignored;
            std::filesystem::remove(first, ignored); std::filesystem::remove(second, ignored);
            std::filesystem::remove(directory / "a", ignored); std::filesystem::remove(directory / "b", ignored);
            std::filesystem::remove(directory, ignored);
        }

        char summary[160]{};
        std::snprintf(summary, sizeof(summary),
            "  실사 단정 %zu/%zu\n", check.passed,
            check.passed + check.failed);
        outLog += summary;
        return check.failed == 0u;
    }
}
