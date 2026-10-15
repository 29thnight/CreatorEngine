# reflgen 통합 증거와 현재 저장 경계

기준: master `493536e300e274cbf59989cdd62b20310c8ae64f`의 소스와 호출부. 이 문서는 현재 구현을 설명하며, 이번 문서 갱신에서 빌드·테스트·제품 실행을 수행하지 않았다.

## 이력과 현재 구현을 구분한다

이 문서는 reflgen과 당시 PHASE 4.25 통합 보고서의 진입점을 유지한다. 당시 설치·빌드·회귀 집계는 [원자료 JSON](ReflgenPhase425Integration.json)에 남아 있으며 이번 개정에서 변경하지 않았다. 그 자료를 현재 master의 새 실행 결과로 재표시하지 않는다.

현재 generator dependency는 overlay port의 reflgen v1.0.0이다. VSIX 설치 여부나 버전은 이 checkout만으로 현재 사용자의 Visual Studio 상태를 알 수 없으며 엔진 generator와 별도다. 이전 보고서의 특정 PC·설치 인스턴스가 지금도 유효하다고 가정하지 않는다.

## 통합에서 유지해야 할 계약

RenderEngine/SceneRuntime descriptor를 생성한 등록 TU는 ReflgenRegistration.h를 통해 엔진 serializer와 field 정의를 함께 보아야 한다. registration function을 호출하는 것만으로 field serializer 가시성 문제가 사라지지 않으므로 VerifyReflectRegistration을 유지한다.

Material의 graph runtime instance, Foliage의 runtime graph source, SceneRenderProfileComponent의 runtime cache는 저작 필드와 구별해야 한다. 이름만 옛 Volume 계열로 되돌리거나 runtime pointer를 저장하려 해서는 안 된다. 정확한 저장 대상은 현재 선언의 reflect/ignore와 생성 결과로 검토한다. 과거 “필드 N개”는 새 field 변경의 합격 기준이 아니다.

현재 GC 통합에서는 ComponentFactory가 explicit domain으로 managed component를 만든다. reflector allocation patch와 `Meta::Create` 제한도 함께 확인한다. 생성기 교체가 authoring field를 유지하더라도 수명 모델까지 자동으로 유지되는 것은 아니다.

## 다시 확인할 순서

1. vcpkg port·patch와 imported targets identity를 기록한다
2. 생성 입력과 registration TU의 serializer 가시성을 확인한다
3. Debug/Release compile·부팅 검증을 각각 수행한다
4. 저장→재로드, property change, post-load, Editor Undo/Apply를 수행한다
5. 재질 화면·GPU 결과·성능은 해당 렌더 검증에서 별도로 판정한다

이번 작업은 1~2의 소스 연결을 읽은 문서 재작성이다. 기존 JSON의 build 성공이나 Editor 검사를 이번 실행으로 주장하지 않는다. 자세한 사용 계약은 [ReflectionDesign](../design/ReflectionDesign.md)을 따른다.

## 구현 근거

- [ports/reflgen/portfile.cmake](../../ports/reflgen/portfile.cmake)
- [Directory.Build.targets](../../Directory.Build.targets)
- [Engine/RenderEngine/ReflgenRegistration.h](../../Engine/RenderEngine/ReflgenRegistration.h)
- [Engine/SceneRuntime/RegisterReflectManual.h](../../Engine/SceneRuntime/RegisterReflectManual.h)
- [Engine/SceneRuntime/ComponentFactory.cpp](../../Engine/SceneRuntime/ComponentFactory.cpp)
