# SceneRuntime과 CLR bridge 모듈 경계

기준: master `493536e300e274cbf59989cdd62b20310c8ae64f`의 소스와 호출부. 이 문서는 현재 구현을 설명하며, 이번 문서 갱신에서 빌드·테스트·제품 실행을 수행하지 않았다.

## 현재 이름과 빌드 단위

이 파일은 예전 ScriptBinder 분할 분석의 진입점이다. 현재 native project는 `Engine/SceneRuntime/SceneRuntime.vcxproj`이며 Scene·Entity·컴포넌트·lifecycle·animation·audio와 ClrHost/ScriptComponent를 함께 포함한다. 옛 ScriptBinder 경로를 현재 project 경로로 안내하지 않는다.

관리 코드의 별도 build unit은 ScriptCore와 ScriptCore.Generators다. generator는 analyzer 참조이며 runtime assembly dependency가 아니다. ClrHost가 native scene objects와 value ABI, managed entrypoints를 이어준다. RuntimeHost는 bootstrap/config/command core의 경계이며 SceneRuntime 전체를 대신하는 “script 전용 라이브러리”가 아니다.

## 왜 ClrHost만 옮기면 끝나지 않는가

ClrHost.cpp는 Transform/UI/physics/audio/material/input을 handle로 resolve하는 native function table을 만든다. ScriptComponent는 Scene의 단계 전이를 관리 instance로 전달한다. RuntimeFrame은 등록·접촉·애니메이션·named message·AI tick flush 순서를 소유한다. 파일 이동만 하면 이 순서와 shared type/layout 의존이 없어지지 않는다.

반대로 Physics의 SDK 경계는 이미 별도 project/API이며 AudioBackend는 vendor를 숨긴다. 외부 의존을 분리할 때 domain-specific service와 scene handle resolution을 나눠야 한다. 전역 Scene 접근을 새 DLL 하나로 감싸는 방식은 수명 문제를 해결하지 않는다.

## 현재 소유권이 주는 제한

SceneManager가 GCCE domain을 공유하고 Scene/Entity/Component graph는 explicit root/trace 관계를 가진다. 새 모듈에서 managed object를 generic reflection allocator로 만들거나 별도 domain으로 DDOL을 보내면 기존 수명 계약을 위반한다. registry가 DLL마다 분리되지 않도록 reflgen registry의 out-of-line 정의도 유지한다.

## 후속 분할의 수용 조건

분할은 아직 별도 완료 결정이 아니다. 새 target을 만들려면 public ABI 타입, 생성/종료 순서, Scene resolution, managed loader/reload, profiler·diagnostics, Editor/Player 링크 폐포를 명세한다. 최소 확인은 두 host의 동일 RuntimeFrame 순서, native/C# table version/size, assembly reload와 shutdown drain이다. 이번 문서는 project와 호출부만 확인했으며 재링크·성능 비교를 실행하지 않았다.

## 구현 근거

- [Engine/SceneRuntime/SceneRuntime.vcxproj](../../Engine/SceneRuntime/SceneRuntime.vcxproj)
- [Engine/SceneRuntime/ClrHost.cpp](../../Engine/SceneRuntime/ClrHost.cpp)
- [Engine/SceneRuntime/ScriptComponent.cpp](../../Engine/SceneRuntime/ScriptComponent.cpp)
- [Engine/SceneRuntime/RuntimeFrame.cpp](../../Engine/SceneRuntime/RuntimeFrame.cpp)
- [Engine/SceneRuntime/SceneManager.h](../../Engine/SceneRuntime/SceneManager.h)
- [ScriptCore/ScriptCore.csproj](../../ScriptCore/ScriptCore.csproj)
- [Engine/RuntimeHost/EngineBootstrap.h](../../Engine/RuntimeHost/EngineBootstrap.h)
