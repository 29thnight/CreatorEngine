#pragma once
#ifndef DYNAMICCPP_EXPORTS
#include <atomic>
#include <cstdint>
#include <cstring>
#include <string>
#include <typeinfo>

// 생명주기 호출 순서 기록기 (PHASE 9-0).
//
// ── 왜 필요한가 ──
//
// PHASE 9는 컴포넌트 생명주기를 델리게이트 구독에서 Scene 소유 단계 리스트로
// 갈아엎는다. 그 교체가 "동작을 바꾸지 않았다"를 주장하려면 비교 대상이 있어야 하는데,
// 지금 순서는 어디에도 적혀 있지 않다 — 델리게이트의 우선순위 정렬과 등록 시점이
// 만들어 내는 창발적 결과라, 코드를 읽어서는 알 수 없고 돌려 봐야만 안다.
//
// 그래서 교체 **전에** 한 번 받아 적는다. 교체 후 같은 시나리오로 다시 받아
// 두 파일을 diff 하는 것이 9-1~9-3의 기능 동등성 판정 방법이다.
//
// ── 왜 틱 단계는 프레임 예산을 두는가 ──
//
// Awake·Start·OnDestroy는 객체당 한 번이라 전부 적어도 파일이 유한하다.
// 반면 Update는 프레임마다 컴포넌트 수만큼 나오므로 그대로 적으면 몇 초 만에
// 수백만 줄이 되고, 그런 파일은 diff가 불가능하다.
//
// 알고 싶은 것은 "한 프레임 안에서 누가 어떤 순서로 불리는가"이지 그것이
// 몇 번 반복되는지가 아니다. 그래서 켤 때 프레임 수를 받아 그동안만 적는다.
//
// ── 스레드 ──
//
// 게임 스레드 전용을 전제하지 않는다. 현재 브로드캐스트 경로에는 AI future처럼
// 다른 스레드에서 들어올 여지가 있고, 기록기가 그걸 만나 깨지면 진단 도구가
// 진단 대상이 되어 버린다. 켜져 있을 때만 락을 잡는다 — 꺼져 있으면
// 원자 변수 한 번 읽는 것이 전부라 상시 컴파일해 두어도 부담이 없다.
namespace Lifecycle
{
    // 라벨은 **실제로 불린 훅의 이름**이다 (트랙 C · C5).
    //
    // 예전에는 옛 3훅 이름(Awake·Start·OnDestroy)을 달았다. L3이 Component의 축을
    // 6단계로 바꾼 뒤에도 라벨만 남아, 기준선이 "무엇이 실제로 불렸는가"를 말하지
    // 못했다 — Canvas의 `Awake` 사건이 그 실례다(C3 4차 기록 참고).
    //
    // 나열 순서는 한 컴포넌트가 겪는 시간 순서다. 틱 셋(FixedUpdate·Update·
    // LateUpdate)은 훅이 아니라 **프레임 페이즈**이므로 이름이 그대로 옳다 —
    // C4에서 Scene의 틱 셋을 건드리지 않은 것과 같은 사유다.
    enum class Phase : uint8_t
    {
        OnInitialized,
        OnAddedToScene,
        OnBeginSimulation,
        OnEnable,
        FixedUpdate,
        Update,
        LateUpdate,
        OnDisable,
        OnEndSimulation,
        OnRemovingFromScene,
        OnUninitializing,
    };

    const char* ToString(Phase phase) noexcept;

    class Trace
    {
    public:
        /// 꺼져 있으면 원자 변수 한 번 읽고 끝난다. 기록 지점은 이것부터 본다.
        static bool IsEnabled() noexcept
        {
            return s_enabled.load(std::memory_order_relaxed);
        }

        /// tickFrames: Update/LateUpdate/FixedUpdate를 기록할 프레임 수.
        /// 0이면 수명 단계(위 Phase의 틱 셋을 뺀 나머지)만 적는다.
        static void Enable(int tickFrames);
        static void Disable();
        static void Clear();

        /// 프레임 경계에서 한 번 부른다(ConsoleCommandSystem::Pump 선두).
        /// 틱 예산을 깎고 프레임 구분자를 남긴다.
        static void BeginFrame();

        /// 기록용 타입 이름 — 트레이스를 남기는 모든 경로가 **같은 문자열**을 써야 한다.
        ///
        /// 원래 Scene.cpp의 익명 네임스페이스에 있었다. 그때는 기록 지점이
        /// Scene::RegistryTick 하나뿐이라 그걸로 충분했다. 트랙 C3가 틱을 전용
        /// 시스템으로 옮기면서 기록 지점이 여러 파일로 흩어졌고, 각자 복제하면
        /// 기준선 대조의 전제("두 경로가 같은 문자열을 남긴다")부터 흔들린다.
        /// (위 TypeName<T>()는 컴파일 타임 타입을 아는 경로용 — 이쪽은 Component*뿐인
        ///  경로용이고, 리플렉션 레지스트리의 이름을 쓴다.)
        static const char* TypeNameOf(class Component* component);

        /// typeName은 정적 문자열이어야 한다(컴파일 타임 타입 이름).
        /// objectName/instanceId는 같은 타입이 여럿일 때 줄을 가르기 위한 것이다.
        static void Record(Phase phase, const char* typeName,
                           const char* objectName, uint64_t instanceId);

        /// 기록을 파일로 쓴다. 성공하면 true.
        static bool Dump(const std::string& path);

        static size_t Count();
        static int RemainingTickFrames() noexcept;

    private:
        static std::atomic<bool> s_enabled;
    };

    /// 컴파일 타임 타입 이름. MSVC의 typeid().name()은 "class Animator" 형태라
    /// 접두사를 한 번만 걷어 정적 저장소에 담아 둔다 — 기록 지점마다 문자열을
    /// 만들면 계측이 측정 대상을 바꾼다. 반환 포인터는 프로그램 수명 내내 유효하다.
    template<typename T>
    const char* TypeName()
    {
        static const std::string cached = []
        {
            std::string name = typeid(T).name();
            for (const char* prefix : { "class ", "struct " })
            {
                const size_t length = std::strlen(prefix);
                if (name.size() > length && 0 == name.compare(0, length, prefix))
                {
                    name.erase(0, length);
                    break;
                }
            }
            return name;
        }();
        return cached.c_str();
    }
}

/// 기록 지점에서 쓰는 헬퍼. 꺼져 있을 때 인자 계산조차 하지 않게 한다 —
/// objectName을 얻으려면 소유자를 역참조해야 하는데, 그 비용을 상시 경로에
/// 남기면 계측이 측정 대상을 바꾼다.
#define LIFECYCLE_TRACE(phase, typeName, objectNameExpr, instanceIdExpr)            \
    do {                                                                            \
        if (::Lifecycle::Trace::IsEnabled())                                        \
        {                                                                           \
            ::Lifecycle::Trace::Record((phase), (typeName),                         \
                (objectNameExpr), (instanceIdExpr));                                \
        }                                                                           \
    } while (false)

#else // DYNAMICCPP_EXPORTS

// C++ 스크립트 모듈(Dynamic_CPP)은 기록기 구현을 링크하지 않는다.
// 헤더가 딸려 들어와도 컴파일이 깨지지 않도록 빈 매크로를 둔다.
// PHASE 9-4에서 이 모듈이 은퇴하면 이 분기도 함께 사라진다.
#define LIFECYCLE_TRACE(phase, typeName, objectNameExpr, instanceIdExpr) ((void)0)

#endif // !DYNAMICCPP_EXPORTS
