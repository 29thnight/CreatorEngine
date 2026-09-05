#pragma once
// LC3 (PHASE 14.5) — 명령 schema 의 단일 정본.
//
// ── 무엇이 갈라져 있었나 ────────────────────────────────────────────────
//
// `PrintHelp()` 는 손으로 쓴 긴 문자열 하나였고 registry 는 별도 표였다. 둘을
// 잇는 것이 없어서 조용히 벌어졌다 — LC0 실측(§2.1.1):
//
//   · 등록 205 개 중 help 에 실린 것 **130 개(63%)**.
//   · help 가 안내하는데 **등록돼 있지 않은 이름 6 개**
//     (`experiment.anim` · `bench` · `fbx` · `gltf` · `import` · `model`).
//     help 를 보고 그대로 치면 "알 수 없는 명령"이 뜬다.
//
// 두 번째가 특히 나쁘다. 문서가 없는 것보다 **틀린 문서가 있는 것**이 나쁘고,
// 그 틀림을 아무도 알아채지 못한 이유는 대조할 것이 없었기 때문이다.
//
// descriptor 는 그 대조를 구조적으로 없앤다 — help 가 descriptor 에서 **생성**
// 되므로 갈라질 자리가 없다. `GET /commands`(LC4)도 같은 snapshot 을 낸다.
//
// ── 등록에 summary 와 cost 를 강제한다 ──────────────────────────────────
//
// 서명이 요구하지 않으면 아무도 안 쓴다. 지금 78 개가 help 에 없는 이유가 그것이다.
// 새 명령은 descriptor 없이 등록할 수 없다(§13 LC3 완료 기준).

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace CommandCore
{
    /// 명령이 얼마나 걸리는가. **서비스가 동기/202 를 추측하지 않게 하는 값이다**(§6.2).
    ///
    /// 짐작이 아니라 판정이어야 한다. 틀릴 때는 **비싼 쪽으로 틀리는 것이 안전하다** —
    /// `Long` 을 `Immediate` 로 잘못 적으면 지연 계약이 깨지지만, 그 반대는
    /// 응답이 202 로 한 번 더 도는 것뿐이다.
    enum class CommandCost : uint8_t
    {
        /// 같은 프레임에서 끝난다. 상태 조회·토글·설정.
        Immediate,

        /// 몇 프레임을 쓴다. 씬 조작·프리팹 저작처럼 GT 작업이 실제로 도는 것.
        Frames,

        /// 초 단위로 간다. 빌드·전수 코퍼스·임포트·렌더 스윕.
        /// LC5 에서 operationId 로 승격될 대상이다.
        Long,
    };

    std::string_view ToString(CommandCost cost) noexcept;

    /// 어느 실행체에 등록되는가(§6.2 · §11.2).
    ///
    /// Player registry 는 `Player` 가 없는 명령을 **애초에 등록하지 않는다** —
    /// 런타임 거부가 아니라 부재다. 오늘 Editor 전용이 아닌 명령은 없으므로
    /// 전부 `Editor` 이고, LC8 이 Player 에 열 것을 골라 `Both` 로 바꾼다.
    enum class CommandRoles : uint8_t
    {
        Editor = 1 << 0,
        Player = 1 << 1,
        Both   = Editor | Player,
    };

    constexpr bool HasRole(CommandRoles roles, CommandRoles role) noexcept
    {
        return 0 != (static_cast<uint8_t>(roles) & static_cast<uint8_t>(role));
    }

    std::string_view ToString(CommandRoles roles) noexcept;

    /// 이 명령이 GUI 와 **어디까지 같아야 하는가**(§9).
    ///
    /// ★ 서비스가 이 분류를 급하게 만들었다. 사람이 `--exec` 로 한 번 부르던
    ///   것과 에이전트가 초당 여러 번 부르는 것은 Undo 스택에 남기는 흔적이
    ///   다르다. 어느 명령이 GUI 와 Undo 까지 맞아야 하고 어느 것이 결과만
    ///   맞으면 되는지가 표에 없으면, 동등성 검사를 무엇에 걸어야 할지 아무도
    ///   모른다.
    enum class CommandClass : uint8_t
    {
        /// GUI 와 Undo·selection·transaction 까지 같아야 한다.
        /// play/stop, 일반 duplicate, undo/redo.
        EditorOperation,

        /// GUI 와 low-level 결과만 같으면 된다.
        /// scene load/save, prefab save, script reload, 라이브 조회·조정.
        EngineService,

        /// GUI 의미가 없고 관측·격리 검사용이다. `dx12.*`, `vk.*`, `profile.*`.
        Probe,

        /// 회귀 fixture 를 만들려고 Undo 를 **의도적으로 우회**한다.
        /// 표기되지 않으면 일반 회귀와 섞여, 동등성을 검사해야 할 것과
        /// 검사하면 안 되는 것을 가릴 수 없다. 대부분 `*.authoring.probe`.
        RawFixture,
    };

    std::string_view ToString(CommandClass cls) noexcept;

    /// 켜져 있는 프로세스에서 되는가.
    ///
    /// ★ **기본은 라이브다.** 요구가 "특정 명령을 제외하면 에디터·플레이어를
    ///   껐다 켜지 않고 동작해야 한다"이므로, 예외인 쪽이 표기 대상이다.
    ///
    /// ★★ 이 값은 **실측에서 왔다.** 착수 때는 `dx12.*`·`vk.*` 56 개가 전부
    ///   프로세스를 새로 띄워야 하는 줄 알았다 — 하네스가 그렇게 부르고 있어서
    ///   그렇게 읽었다. 켜져 있는 에디터에 HTTP 로 64 개를 붙여 보니 61 개가
    ///   그대로 돌았고, 못 도는 셋은 전부 디바이스·스왑체인을 **세우는** 것이었다.
    ///   하네스가 프로세스를 새로 띄우는 것은 격리 관행이지 재시작 요구가 아니다.
    enum class CommandLiveness : uint8_t
    {
        /// 켜져 있는 프로세스에서 그대로 된다.
        Live,

        /// 이미 초기화된 프로세스에서는 안 된다. 디바이스 브링업 프로브.
        RequiresRestart,

        /// 부르면 프로세스가 끝난다. 서비스로 부르면 응답을 못 받는다.
        TerminatesProcess,
    };

    std::string_view ToString(CommandLiveness liveness) noexcept;

    /// 명령 하나의 schema.
    ///
    /// argument·capability 는 LC3 범위 밖이다(§13 은 그것들을 여기 두라고 하지만,
    /// 208 개의 인자 스키마를 한 슬라이스에서 정확히 적는 것은 사실상 지어내기가
    /// 된다). 지금은 이름·별칭·요약·비용·역할까지를 정본으로 세우고, 인자
    /// 스키마는 domain 을 옮기는 LC6 에서 그 domain 을 아는 사람이 채운다.
    /// 그 자리를 비워 두되 **비어 있다는 사실이 보이게** 한다.
    struct CommandDescriptor
    {
        std::string              canonical;
        std::vector<std::string> aliases;

        /// 한 줄 요약. help 와 discovery 가 이것을 쓴다. 비어 있으면 등록 실패다.
        std::string              summary;

        /// 인자 사용법. `<필수> [선택]` 형태. 없으면 빈 문자열.
        std::string              usage;

        CommandCost              cost{ CommandCost::Immediate };
        CommandRoles             roles{ CommandRoles::Editor };

        /// §9 분류와 라이브 여부. seed 가 정한다 — 여기 기본값이 있는 이유는
        /// descriptor 가 seed 없이 만들어지는 경로(등록 거부 직전)뿐이다.
        CommandClass             cls{ CommandClass::EngineService };
        CommandLiveness          liveness{ CommandLiveness::Live };

        /// 이 명령이 **사용자 코드를 부르는가**(§6.2 의 `ExecutesUserCode` capability).
        ///
        /// ★ §6.2 는 capability 를 열셋짜리 비트마스크로 그린다. 그중 **하나만**
        ///   여기 서 있는 이유는 나머지 열둘이 아직 쓰이는 자리가 없기 때문이다 —
        ///   `RequiresDX12` 같은 것을 지금 212 개에 적으면 그것은 관측이 아니라
        ///   지어내기가 된다(LC3 이 인자 스키마를 비워 둔 것과 같은 판단이다).
        ///   이 하나는 다르다. LC7 이 L3 등급 B 를 열면서 **승인 경계를 그을
        ///   유일한 표식**이 되고, 그 경계는 오늘 존재한다.
        ///
        /// 두 곳이 이 값을 읽는다. ① `ExecuteParsed` 가 `ClrHost::UserCodeScope` 를
        /// 이 값일 때만 연다 — 그래서 표시 없는 명령은 사용자 코드에 **도달하지
        /// 못한다**. ② 서비스가 별도 플래그 없이 이 명령을 받으면 403 이다(§8).
        bool                     executesUserCode{ false };

        /// 결과를 내는 핸들러인가(LC1 이행 여부). discovery 가 그대로 낸다.
        bool                     resultBearing{ false };

        /// 예외 경계를 통과시키는가(`crash.test`).
        bool                     exceptionsEscape{ false };

        /// 등록 순서. snapshot 정렬은 이름으로 하지만, 원본 순서도 남긴다.
        std::size_t              registrationIndex{};
    };
}
