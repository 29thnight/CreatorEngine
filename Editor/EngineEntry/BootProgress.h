#pragma once
#include "ProgressWindow.h"
#include <algorithm>

/// 부팅 진행률 추적자.
///
/// 예전에는 SetProgress(10), SetProgress(50)... 하드코딩 퍼센트가 App.cpp와
/// Dx11Main.cpp에 흩어져 있어, 부팅 단계를 하나 넣거나 빼면 숫자를 전부
/// 다시 배분해야 했다. 이제 부팅 경로는 Step()만 부르고 퍼센트는
/// 단계 수로 자동 계산한다.
///
/// 라이트맵 베이킹·셰이더 리로드·스크립트 핫리로드는 자체 퍼센트로
/// g_progressWindow를 직접 쓰므로 여기를 거치지 않는다.
class BootProgress
{
public:
    /// 에디터 부팅 경로의 Step() 호출 총수. 단계를 추가/제거하면 이 수만 맞춘다.
    /// 어긋나도 클램프 덕에 바가 100을 넘거나 후퇴하지는 않는다.
    static constexpr int kEditorBootSteps = 12;

    static void Begin(int totalSteps)
    {
        s_total = totalSteps > 0 ? totalSteps : 1;
        s_done = 0;
        g_progressWindow->SetProgress(0);
    }

    /// 다음 단계에 진입한다. 상태 텍스트를 표시하고 완료된 단계 비율로 바를 채운다.
    static void Step(const std::wstring& statusText)
    {
        g_progressWindow->SetStatusText(statusText);

        const int percent = std::min(99, (s_done * 100) / s_total);
        g_progressWindow->SetProgress(percent);
        ++s_done;
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
