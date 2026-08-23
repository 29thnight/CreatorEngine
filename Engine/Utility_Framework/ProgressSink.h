#pragma once
#include <functional>
#include <string>

/// 진행률 게시 지점 (L1-3c).
///
/// 셰이더 리로드처럼 아래층에서 오래 걸리는 작업이 진행률을 알리고 싶을 때,
/// 예전에는 EngineEntry/ProgressWindow.h(최상층 Win32 창)를 직접 include했다 —
/// 렌더가 실행 파일 셸을 아는 역방향 간선이다. 방향을 뒤집는다: 아래층은
/// 여기에 "게시"만 하고, 그것을 어디에 어떻게 그릴지는 최상층이 부팅 때
/// 싱크를 등록해서 정한다(App.cpp). 싱크가 비어 있으면 전부 무해한 no-op이라
/// 게임 빌드처럼 진행 창이 없는 환경에서도 그대로 동작한다.
namespace Progress
{
    struct Sink
    {
        std::function<void()> launch;
        std::function<void(const std::wstring&)> setTitle;
        std::function<void(const std::wstring&)> setStatus;
        std::function<void(float)> setProgress;
        std::function<void()> close;
    };

    inline Sink& GetSink()
    {
        static Sink s_sink;
        return s_sink;
    }

    inline void Launch()                            { if (GetSink().launch) GetSink().launch(); }
    inline void SetTitle(const std::wstring& text)  { if (GetSink().setTitle) GetSink().setTitle(text); }
    inline void SetStatus(const std::wstring& text) { if (GetSink().setStatus) GetSink().setStatus(text); }
    inline void SetProgress(float percent)          { if (GetSink().setProgress) GetSink().setProgress(percent); }
    inline void Close()                             { if (GetSink().close) GetSink().close(); }
}
