#include "ExperimentParity/ExperimentVertexLayoutSelfTest.h"

#include "RHI/ModelVertexInputLayout.h"
#include "Mesh.h" // legacy ::Vertex — 전환표 단정용

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdio>
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

            void Check(bool condition, const std::string& label)
            {
                if (condition)
                {
                    ++passed;
                    return;
                }
                ++failed;
                log += "    [실패] " + label + "\n";
            }
        };

        [[nodiscard]] bool ElementIs(const RHIInputElement& element,
            const char* semantic, std::uint32_t semanticIndex, RHIFormat format,
            std::uint32_t offset)
        {
            return std::string(element.semantic) == semantic
                && element.semanticIndex == semanticIndex
                && element.format == format
                && element.alignedByteOffset == offset
                && element.inputSlot == 0
                && element.instanceDataStepRate == 0;
        }
    }

    bool RunExperimentVertexLayoutSelfTest(std::string& outLog)
    {
        Checker check{ outLog };
        outLog += "[assets.modelrender] MBC6 model mask→RHI/PSO 계약\n";
        std::string error;

        // ── 1. core(정적 48B) ─────────────────────────────────────────────
        {
            std::vector<RHIInputElement> elements;
            check.Check(ModelVertexInput::BuildInputElements(
                assets::kCoreVertexAttributes, elements, error),
                "core 마스크 유도 (" + error + ")");
            check.Check(4u == elements.size(), "core 원소 수 4");
            if (4u == elements.size())
            {
                check.Check(ElementIs(elements[0], "POSITION", 0,
                    RHIFormat::RGB32Float, 0), "POSITION RGB32 @0");
                check.Check(ElementIs(elements[1], "NORMAL", 0,
                    RHIFormat::RGB32Float, 12), "NORMAL RGB32 @12");
                check.Check(ElementIs(elements[2], "TEXCOORD", 0,
                    RHIFormat::RG32Float, 24), "TEXCOORD0 RG32 @24");
                check.Check(ElementIs(elements[3], "TANGENT", 0,
                    RHIFormat::RGBA32Float, 32),
                    "TANGENT RGBA32(w=handedness) @32");
            }
            check.Check(48u == assets::StrideOf(
                assets::kCoreVertexAttributes), "core stride 48");
        }

        // ── 2. V2(스킨 68B) ───────────────────────────────────────────────
        {
            std::vector<RHIInputElement> elements;
            check.Check(ModelVertexInput::BuildInputElements(
                assets::kV2VertexAttributes, elements, error),
                "스킨 마스크 유도 (" + error + ")");
            check.Check(6u == elements.size(), "스킨 원소 수 6");
            if (6u == elements.size())
            {
                check.Check(ElementIs(elements[4], "BLENDINDICES", 0,
                    RHIFormat::RGBA8Uint, 48), "BLENDINDICES RGBA8Uint @48");
                check.Check(ElementIs(elements[5], "BLENDWEIGHT", 0,
                    RHIFormat::RGBA32Float, 52), "BLENDWEIGHT RGBA32 @52");
            }
            check.Check(68u == assets::StrideOf(
                assets::kV2VertexAttributes), "스킨 stride 68");
        }

        // ── 3. uv1(라이트맵) — 뒤 속성 오프셋이 표에서 밀린다 ─────────────
        {
            const assets::VertexAttributeMask mask =
                assets::kCoreVertexAttributes
                | assets::Bit(assets::VertexAttribute::Uv1);
            std::vector<RHIInputElement> elements;
            check.Check(ModelVertexInput::BuildInputElements(mask,
                elements, error), "uv1 마스크 유도 (" + error + ")");
            check.Check(5u == elements.size(), "uv1 원소 수 5");
            if (5u == elements.size())
            {
                check.Check(ElementIs(elements[3], "TEXCOORD", 1,
                    RHIFormat::RG32Float, 32), "TEXCOORD1 RG32 @32");
                check.Check(ElementIs(elements[4], "TANGENT", 0,
                    RHIFormat::RGBA32Float, 40), "uv1이 TANGENT를 @40으로 민다");
            }
        }

        // ── 4. fail-closed ────────────────────────────────────────────────
        {
            std::vector<RHIInputElement> elements;
            check.Check(!ModelVertexInput::BuildInputElements(
                assets::Bit(assets::VertexAttribute::Position),
                elements, error) && !error.empty(),
                "core 결손 마스크는 거부");
            check.Check(!ModelVertexInput::BuildInputElements(
                assets::kCoreVertexAttributes
                | assets::Bit(assets::VertexAttribute::BoneIndices),
                elements, error) && !error.empty(),
                "skin 반쪽 마스크는 거부");
            check.Check(!ModelVertexInput::BuildInputElements(
                assets::kCoreVertexAttributes | (1u << 30), elements,
                error) && !error.empty(),
                "표 밖 비트는 거부");
        }

        // ── 5. 전환표 — legacy ::Vertex(96B) 대비 D4가 지킬 계약 ──────────
        //
        // 유도 레이아웃은 legacy와 오프셋 호환이 아니다. 이 다름이 명세다:
        // 레이아웃만 먼저 바꾸면 96B 버퍼와 어긋난다(D2 실측 — 전환은 D4에서
        // 정점 버퍼와 동시).
        {
            check.Check(offsetof(::Vertex, tangent) == 40u
                && offsetof(::Vertex, bitangent) == 52u,
                "legacy: TANGENT RGB32 @40 + BINORMAL @52 (유도: RGBA32 @32,"
                " BINORMAL 없음 — 셰이더가 cross(N,T)*w 재구성)");
            check.Check(offsetof(::Vertex, boneIndices) == 64u
                && sizeof(::Vertex{}.boneIndices) == 16u,
                "legacy: BLENDINDICES RGBA32Float @64 (유도: RGBA8Uint @48 —"
                " 셰이더가 uint로 읽는다)");
            check.Check(sizeof(::Vertex) == 96u
                && assets::StrideOf(assets::kV2VertexAttributes) == 68u,
                "stride 96 → 68 — 버퍼와 레이아웃은 동시에만 바뀐다");
        }

        // core/color/skin 및 UV1 조합이 layout과 shader permutation 양쪽에서 서로 다른
        // key를 가져야 한다. skin bool 하나로 축약하면 이 단정이 붉어진다.
        {
            constexpr std::array<std::uint32_t, 8> expectedStride{{ 48u, 64u, 68u, 84u, 56u, 72u, 76u, 92u }};
            constexpr std::array<std::size_t, 8> expectedElements{{ 4u, 5u, 6u, 7u, 5u, 6u, 7u, 8u }};
            static_assert(expectedStride.size() == assets::kModelVertexMasks.size());
            static_assert(expectedElements.size() == assets::kModelVertexMasks.size());
            std::vector<RHIShaderPermutationKey> keys;
            for (std::size_t i = 0; i < assets::kModelVertexMasks.size(); ++i)
            {
                const auto mask = assets::kModelVertexMasks[i];
                std::vector<RHIInputElement> elements;
                check.Check(ModelVertexInput::BuildInputElements(mask, elements, error),
                    "model mask layout " + std::to_string(mask));
                check.Check(elements.size() == expectedElements[i],
                    "model mask element count " + std::to_string(mask));
                check.Check(assets::StrideOf(mask) == expectedStride[i],
                    "model mask stride " + std::to_string(mask));
                RHIShaderPermutation permutation;
                check.Check(ModelVertexInput::ApplyShaderPermutation(mask,
                    permutation, error), "model mask shader axes "
                        + std::to_string(mask));
                keys.push_back(permutation.Key());
            }
            std::sort(keys.begin(), keys.end(), [](const auto& left, const auto& right)
                { return left.hi != right.hi ? left.hi < right.hi : left.lo < right.lo; });
            check.Check(std::adjacent_find(keys.begin(), keys.end()) == keys.end(),
                "core/color/skin/color+skin 및 UV1 PSO key 8종 분리");
            check.Check(assets::StrideOf(assets::kCoreColorSkinVertexAttributes) == 84u
                && assets::OffsetOf(assets::kCoreColorSkinVertexAttributes,
                    assets::VertexAttribute::BoneIndices) == 64u
                && assets::OffsetOf(assets::kCoreColorSkinVertexAttributes,
                    assets::VertexAttribute::BoneWeights) == 68u,
                "SU full mask stride 84/bone offsets 64,68");
        }

        char summary[120]{};
        std::snprintf(summary, sizeof(summary), "  합성 단정 %zu/%zu\n",
            check.passed, check.passed + check.failed);
        outLog += summary;
        return check.failed == 0u;
    }

    bool RunModelRenderWiringSelfTest(std::string& outLog)
    {
        return RunExperimentVertexLayoutSelfTest(outLog);
    }
}
