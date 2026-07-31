#ifndef DYNAMICCPP_EXPORTS
#include "EngineBootstrap.h"

// 종료 추적의 마지막 표식.
//
// `[8] atexit 도달`은 CRT 정리가 *시작*될 때 찍힌다. 그 뒤에도 정적 소멸자,
// DLL 언로드, 힙 파괴가 남아 있어서 거기서 죽어도 추적의 마지막 줄은 여전히
// `[8]`이다 — 정상 종료와 구분이 되지 않는다.
//
// 실제로 이 사각지대 때문에 종료 시 힙 손상(0xC0000374)을 쫓을 때 '어디까지
// 갔는지'를 알 수 없었다. 573개 세션을 훑어도 비정상은 전부 FinalizeRuntime
// 안쪽이었고, 정작 찾던 종료 구간 크래시는 정상 종료와 같은 모양이었다.
//
// init_seg(lib)로 사용자 정적 객체보다 먼저 생성해 두면 가장 나중에 소멸한다.
// 추적에 이 줄이 없으면 정적 소멸 구간에서 죽은 것이고, 있으면 그 뒤(힙·DLL
// 정리)에서 죽은 것이다. 둘은 원인이 다르므로 구분이 곧 다음 수사 방향이다.
#pragma warning(push)
#pragma warning(disable: 4073)  // init_seg(lib)은 라이브러리용 — 여기서는 의도한 사용이다
#pragma init_seg(lib)
#pragma warning(pop)

namespace
{
    struct ShutdownFinalMarker
    {
        ~ShutdownFinalMarker()
        {
            EngineBootstrap::ShutdownTrace("[9] 정적 소멸 완료 (이후 힙·DLL 정리)");
        }
    };

    ShutdownFinalMarker g_shutdownFinalMarker;
}

#endif // DYNAMICCPP_EXPORTS
