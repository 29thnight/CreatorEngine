#pragma once

#include "Experiment/ModelLoader.h"

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

class Model;
class Skeleton;
class Bone;
struct Vertex;
struct MaterialPropertyValue;

// legacy Model → experiment::ModelDraft 브리지. 패리티·재생·벤치 검사가 공유한다.
// 실제 Assimp/cooked decoder가 생기면 LegacyBridgeDecoder 자리만 그 구현으로
// 바꾸면 legacy 대 독립 디코드 비교로 승격된다.
namespace RenderTest::bridge
{
    struct BridgeReport final
    {
        std::vector<std::string> notes{};
        std::vector<std::string> failures{};
        std::size_t totalNodeAnimChannels{};
        std::size_t droppedNodeAnimChannels{};
    };

    // legacy skeleton의 bone 배열은 위상 정렬을 보장하지 않는다(실측: 62개 중
    // 3개 위반). Experiment 계약(parent < index)은 디코더가 정렬로 만족시켜야
    // 하며, 브리지도 같은 정렬을 수행한다. oldToNew: legacy index → 게시 index.
    struct BoneRemap final
    {
        std::vector<std::uint32_t> oldToNew{};
        std::vector<std::uint32_t> newToOld{};
        bool reordered{};

        [[nodiscard]] bool Empty() const { return oldToNew.empty(); }
    };

    [[nodiscard]] BoneRemap ComputeBoneRemap(
        const ::Skeleton& legacySkeleton, BridgeReport& report);

    [[nodiscard]] experiment::Vertex ConvertVertex(
        const ::Vertex& source, const BoneRemap& remap);

    // M5-A property block(고정 필드: numeric/int/bool/textureGuid) → Experiment
    // variant 정본화. ShaderMeta 없이 결정 가능한 규칙만 쓴다:
    //   texture guid 유효 → TextureReference, numeric 1~4개 → float/math::vector2/3/4,
    //   그 외 → int32(비0) → bool(true) → int32(0) 순. 5개 이상 numeric은
    //   variant가 표현하지 못하므로 "unrepresented:float[N]" 문자열 marker로
    //   남긴다(조용한 절단 금지). 실디코더/어댑터는 meta로 타입을 확정해야 한다.
    [[nodiscard]] experiment::MaterialProperty ConvertMaterialProperty(
        const ::MaterialPropertyValue& source);

    [[nodiscard]] experiment::ModelDraft BuildDraftFromLegacy(
        ::Model& legacy, const BoneRemap& remap, BridgeReport& report);

    [[nodiscard]] std::string_view ToString(experiment::ModelLoadIssueCode code);

    // experiment 값 동등성(브리지 왕복 비교용 — 정확 일치).
    [[nodiscard]] bool Eq(const math::vector2& a, const math::vector2& b);
    [[nodiscard]] bool Eq(const math::vector3& a, const math::vector3& b);
    [[nodiscard]] bool Eq(const math::vector4& a, const math::vector4& b);
    [[nodiscard]] bool Eq(const math::matrix4x4& a, const math::matrix4x4& b);
    [[nodiscard]] bool Eq(const experiment::Vertex& a, const experiment::Vertex& b);
    [[nodiscard]] bool Eq(const experiment::TextureReference& a,
        const experiment::TextureReference& b);
    [[nodiscard]] bool Eq(const experiment::MaterialPropertyValue& a,
        const experiment::MaterialPropertyValue& b);

    // draft 를 붙잡았다가 Decode 시점에 복사해 넘긴다(반복 Load 벤치 지원).
    class LegacyBridgeDecoder final : public experiment::IModelDecoder
    {
    public:
        explicit LegacyBridgeDecoder(experiment::ModelDraft&& draft)
            : draft_(std::move(draft))
        {
        }

        experiment::ModelDecodeResult Decode(
            const experiment::ModelLoadRequest&) override
        {
            experiment::ModelDecodeResult result;
            result.draft = draft_;
            return result;
        }

    private:
        experiment::ModelDraft draft_;
    };

    // legacy 로드 → remap → draft → Experiment 게시까지 한 번에.
    struct LoadedPair final
    {
        std::shared_ptr<::Model> legacy{};
        experiment::ModelLoadResult result{};
        BoneRemap remap{};
        BridgeReport report{};
    };

    [[nodiscard]] LoadedPair LoadAndBridge(const std::string& modelPath);

    // notes / failures / validate issues 를 관례 형식으로 로그에 붙인다.
    // 반환값: 실패(브리지 불가 또는 게시 차단) 여부.
    bool AppendOutcome(const LoadedPair& pair, std::string& outLog);
}
