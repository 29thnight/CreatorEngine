#pragma once
#include "ProgressWindow.h"
#include <algorithm>

/// 부팅 진행률 추적자.
///
/// 부팅 경로는 실제 작업 직전에 Step()을 부르고 퍼센트는 단계 수로 계산한다.
/// 같은 단계 안에서 긴 작업의 세부 내용이 바뀌면 Detail()만 갱신한다.
///
/// 라이트맵 베이킹·셰이더 리로드·스크립트 핫리로드는 자체 퍼센트로
/// g_progressWindow를 직접 쓰므로 여기를 거치지 않는다.
class BootProgress
{
public:
    /// 에디터 부팅 경로의 Step() 호출 총수. 단계를 추가/제거하면 이 수만 맞춘다.
    /// 어긋나도 클램프 덕에 바가 100을 넘거나 후퇴하지는 않는다.
    static constexpr int kEditorBootSteps = 23;

    static void Begin(int totalSteps)
    {
        s_total = totalSteps > 0 ? totalSteps : 1;
        s_done = 0;
        g_progressWindow->SetProgress(0);
    }

    /// 다음 단계에 진입한다. 상태 텍스트를 표시하고 완료된 단계 비율로 바를 채운다.
    static void Step(const std::wstring& stage, const std::wstring& detail)
    {
        g_progressWindow->SetBootStatus(stage, detail, s_done + 1, s_total);

        const int percent = std::min(99, (s_done * 100) / s_total);
        g_progressWindow->SetProgress(percent);
        ++s_done;
    }

    /// 같은 단계 안에서 실제 작업이 바뀌면 보조 설명만 갱신한다.
    static void Detail(const std::wstring& detail)
    {
        g_progressWindow->SetBootDetail(detail);
    }

    /// 부팅 완료. 바를 100으로 채운다.
    static void Complete()
    {
        g_progressWindow->SetProgress(100);
    }

private:
    static inline int s_total = 1;
    static inline int s_done = 0;
};
