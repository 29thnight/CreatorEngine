#pragma once
// LC6 (PHASE 14.5) — 도메인 TU 여럿이 함께 쓰는 명령 지원 함수.
//
// 여기 들어오는 기준은 **둘 이상의 도메인이 실제로 쓴다**는 것 하나다.
// 한 도메인만 쓰는 헬퍼는 그 도메인 파일 안에 `static` 으로 남는다 —
// 공용 헤더가 "언젠가 쓸지도 모르는 것"의 창고가 되면 도메인 분리가 이름만
// 남는다.

#include <string>
#include <string_view>

namespace ConsoleCmd
{
    /// 테스트 산출물 경로를 만든다. 상대 경로면 카테고리별 산출물 폴더 밑으로
    /// 붙이고, 부모 디렉터리를 만들어 둔다.
    ///
    /// Core(`lc0`·`lc3`) · RenderTest(`DX12`·`Vulkan`) · Diagnostics(`Traces`)
    /// 가 함께 쓴다.
    std::string ResolveTestArtifactPath(std::string_view category,
                                        std::string_view requestedPath);
}
