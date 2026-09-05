#pragma once
// LC3 (PHASE 14.5) — 명령 schema seed 표의 조회 표면.
//
// 표 본문은 `CommandDescriptorSeeds.cpp` 에 있다. 등록 줄이 아니라 별도 표에
// 둔 이유는 그 파일 머리말에 적었다.

#include "CommandDescriptor.h"

#include <cstddef>
#include <string_view>

namespace CommandCore
{
    struct DescriptorSeed
    {
        const char* name;
        CommandCost cost;
        const char* usage;    ///< `<필수> [선택]`. 없으면 빈 문자열
        const char* summary;  ///< 한 줄. 비어 있으면 등록이 거부된다

        /// ★ 기본값을 **주지 않는다.**
        ///
        ///   LC3 이 같은 실수를 한 번 했다 — 요약을 선택으로 뒀더니 205 개 중
        ///   78 개가 비었고, 아무도 그것을 몰랐다. 서명이 요구하지 않으면 아무도
        ///   안 쓴다. 그래서 이 둘은 표의 **모든 줄이 말하게** 한다. 새 명령을
        ///   더할 때 분류를 빼먹으면 컴파일이 거부한다.
        CommandClass    cls;
        CommandLiveness liveness;

        /// 이 명령이 사용자 코드(관리 어셈블리의 표식된 메서드)를 부르는가.
        ///
        /// ★ 위 둘과 달리 **기본값을 준다.** 이유가 다르기 때문이다.
        ///
        ///   `cls`·`liveness` 는 212 개가 저마다 판정해야 하는 값이라, 기본값을
        ///   주면 아무도 안 쓰고 표가 조용히 거짓이 된다(LC3 이 summary 로 겪었다).
        ///   이것은 반대다 — 오늘 참인 것이 **하나**고, 나머지 211 개에 `false` 를
        ///   적게 하면 그 211 줄이 diff 를 채워 정작 하나뿐인 참을 덮는다.
        ///
        /// ★★ 그래서 위험 방향이 뒤집힌다: 여기서 빠뜨리면 "잠기지 않는" 것이
        ///   아니라 **거부된다.** 표시를 잊은 명령이 사용자 코드를 부르려 하면
        ///   `ClrHost::UserCodeScope` 가 열리지 않아 `NotPermitted` 로 실패한다.
        ///   빠뜨림이 조용한 구멍이 아니라 소리 나는 실패다.
        bool executesUserCode = false;
    };

    /// canonical 이름으로 찾는다. 없으면 nullptr — 그 명령은 등록되지 않는다.
    const DescriptorSeed* FindDescriptorSeed(std::string_view canonical);

    std::size_t DescriptorSeedCount() noexcept;

    /// 표를 순서대로 훑는다. 범위를 벗어나면 nullptr.
    ///
    /// ★ **반대 방향을 볼 수 있어야 한다.** `FindDescriptorSeed` 는 "등록하려는
    ///   이름에 seed 가 있나"를 묻는다. 그 방향만으로는 **seed 는 있는데 등록이
    ///   사라진** 경우를 영영 못 본다 — 등록 줄 하나가 빠지면 그 명령은 조용히
    ///   없어지고, registry 와 help 는 둘 다 그것을 모르므로 자기 일관성 검사는
    ///   전부 초록이다. LC6 이 핸들러 8,700 줄을 도메인 파일로 옮기는데, 등록
    ///   줄을 빠뜨리는 것이 그 작업에서 가장 흔한 사고다.
    const DescriptorSeed* DescriptorSeedAt(std::size_t index) noexcept;
}
