#pragma once

// 쿠킹 포맷의 쓰기/읽기. 포맷 지식은 CookedModelFormat.h 와 이 짝뿐이다.
//
// ★ 이 계층은 **바이트만** 안다. 파일을 열지도, 경로를 만들지도 않는다.
//   쿠킹 산출물이 어디에 놓이는지는 SerializationPlan §3.6.1 소관이고
//   그 결정이 아직 안 났다(GUID 채번이 §3.4 에서 바뀐다). 여기서 경로를
//   정하면 나중에 두 곳을 고치게 된다.
//
// ★ 읽기 실패는 **예외가 아니라 거부**다. 캐시는 언제든 낡거나 잘릴 수 있고
//   그때 할 일은 재임포트이지 중단이 아니다. 그래서 Read 는 issues 를 채우고
//   false 를 돌려준다.

#include "CookedModelFormat.h"
#include "../ModelLoader.h"

#include <cstddef>
#include <span>
#include <string>
#include <vector>

namespace experiment::cooked
{
    struct CookedWriteResult final
    {
        std::vector<std::byte> bytes{};
        std::vector<ModelLoadIssue> issues{};

        [[nodiscard]] bool Succeeded() const noexcept
        {
            return !bytes.empty() && issues.empty();
        }
    };

    // ModelDraft → 바이트. D5부터 writer가 publication gate다. source preview에서
    // 허용되던 fallback path나 nil material/shader identity를 그대로 굽지 않는다.
    // 실패 시 bytes는 비어 있고 issues가 정확한 context를 가진다.
    [[nodiscard]] CookedWriteResult Write(const ModelDraft& draft);

    // 바이트 → ModelDraft.
    //
    // 거부 사유는 issues 에 CookedPayloadRejected(Warning) 로 남는다 — Error 가
    // 아닌 이유는 이것이 정상 폴백 경로이기 때문이다. 로그에서 "캐시가 낡았다"가
    // 실패로 보이면 진짜 실패를 찾을 때 방해가 된다.
    [[nodiscard]] bool Read(std::span<const std::byte> bytes,
        ModelDraft& outDraft, std::vector<ModelLoadIssue>& issues);

    // ModelLoadRequest::cookedPath 를 읽어 Decode 한다.
    // 경로를 **만들지 않는다** — 호출자가 준 것만 연다.
    class CookedModelDecoder final : public IModelDecoder
    {
    public:
        [[nodiscard]] ModelDecodeResult Decode(const ModelLoadRequest& request) override;
    };
}
