#include "ExperimentParity/ExperimentVertexLayoutSelfTest.h"

#include "RHI/ExperimentVertexInputLayout.h"
#include "Mesh.h" // legacy ::Vertex — 전환표 단정용

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
        outLog += "[experiment.vertexlayout] 마스크→RHI 입력 레이아웃 유도\n";
        std::string error;

        // ── 1. core(정적 48B) ─────────────────────────────────────────────
        {
            std::vector<RHIInputElement> elements;
            check.Check(ExperimentVertexInput::BuildInputElements(
                experiment::kCoreVertexAttributes, elements, error),
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
            check.Check(48u == experiment::StrideOf(
                experiment::kCoreVertexAttributes), "core stride 48");
        }

        // ── 2. V2(스킨 68B) ───────────────────────────────────────────────
        {
            std::vector<RHIInputElement> elements;
            check.Check(ExperimentVertexInput::BuildInputElements(
                experiment::kV2VertexAttributes, elements, error),
                "스킨 마스크 유도 (" + error + ")");
            check.Check(6u == elements.size(), "스킨 원소 수 6");
            if (6u == elements.size())
            {
                check.Check(ElementIs(elements[4], "BLENDINDICES", 0,
                    RHIFormat::RGBA8Uint, 48), "BLENDINDICES RGBA8Uint @48");
                check.Check(ElementIs(elements[5], "BLENDWEIGHT", 0,
                    RHIFormat::RGBA32Float, 52), "BLENDWEIGHT RGBA32 @52");
            }
            check.Check(68u == experiment::StrideOf(
                experiment::kV2VertexAttributes), "스킨 stride 68");
        }

        // ── 3. uv1(라이트맵) — 뒤 속성 오프셋이 표에서 밀린다 ─────────────
        {
            const experiment::VertexAttributeMask mask =
                experiment::kCoreVertexAttributes
                | experiment::Bit(experiment::VertexAttribute::Uv1);
            std::vector<RHIInputElement> elements;
            check.Check(ExperimentVertexInput::BuildInputElements(mask,
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
            check.Check(!ExperimentVertexInput::BuildInputElements(
                experiment::Bit(experiment::VertexAttribute::Position),
                elements, error) && !error.empty(),
                "core 결손 마스크는 거부");
            check.Check(!ExperimentVertexInput::BuildInputElements(
                experiment::kCoreVertexAttributes
                | experiment::Bit(experiment::VertexAttribute::BoneIndices),
                elements, error) && !error.empty(),
                "skin 반쪽 마스크는 거부");
            check.Check(!ExperimentVertexInput::BuildInputElements(
                experiment::kCoreVertexAttributes | (1u << 30), elements,
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
                && experiment::StrideOf(experiment::kV2VertexAttributes) == 68u,
                "stride 96 → 68 — 버퍼와 레이아웃은 동시에만 바뀐다");
        }

        char summary[120]{};
        std::snprintf(summary, sizeof(summary), "  합성 단정 %zu/%zu\n",
            check.passed, check.passed + check.failed);
        outLog += summary;
        return check.failed == 0u;
    }
}
