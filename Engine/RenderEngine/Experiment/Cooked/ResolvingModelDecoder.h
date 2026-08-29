#pragma once

#include "../ModelLoader.h"

#include <memory>

namespace experiment::cooked
{
    // cooked decoder 와 source decoder 를 함께 들고 `ModelSourcePreference` 대로
    // 순서를 정하는 decoder.
    //
    // ★ **이것이 없어서 `CookedThenSource` 는 이름만 있었다.**
    //
    //   `ModelLoader` 는 `unique_ptr<IModelDecoder>` 하나만 받는다. 그래서
    //   `SourceOnly`·`CookedOnly` 는 전용 decoder 를 직접 꽂아 검사할 수 있었지만,
    //   **cooked 를 시도하고 거부되면 source 로 넘어가는 경로는 만들 수가
    //   없었다** — 셋을 고르는 주체가 아무 데도 없었기 때문이다
    //   (SerializationPlan §1.2, ModelImportPipelinePlan §1.2).
    //
    // ★ **폴백은 관측 가능해야 한다.**
    //
    //   cooked 가 늘 거부되는데 조용히 source 로 도는 상태는 "느리지만 동작하는"
    //   모습이라 아무도 알아채지 못한다. 이 저장소가 반복해서 밟은 형태다
    //   (legacy 가 `Assets/Models/` 밖 모델에서 캐시를 두고도 매번 Assimp 를
    //   돌던 것이 정확히 그것이다). 그래서 폴백이 일어나면 **Info issue 를
    //   반드시 남기고**, cooked 쪽 issue 도 지우지 않고 함께 보고한다.
    //
    // ★ **cooked 거부는 Error 로 승격하지 않는다.** 포맷 버전이 바뀌었거나
    //   artifact 가 stale 하면 codec 이 `Warning` 으로 거부하는데, 그건 정상적인
    //   재임포트 신호이지 로드 실패가 아니다. 다만 `CookedOnly` 에서는 폴백할
    //   곳이 없으므로 그대로 실패가 된다.
    class ResolvingModelDecoder final : public IModelDecoder
    {
    public:
        // 둘 중 하나는 null 이어도 된다. 없는 쪽을 요구하는 preference 는
        // 실패로 보고한다 — 조용히 다른 쪽으로 넘어가지 않는다. preference 를
        // 무시하는 것은 호출자가 고른 정책을 뒤집는 일이다.
        ResolvingModelDecoder(std::unique_ptr<IModelDecoder> cookedDecoder,
            std::unique_ptr<IModelDecoder> sourceDecoder) noexcept;

        [[nodiscard]] ModelDecodeResult Decode(
            const ModelLoadRequest& request) override;

    private:
        std::unique_ptr<IModelDecoder> cooked_{};
        std::unique_ptr<IModelDecoder> source_{};
    };
}
