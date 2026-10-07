# 스크립트 바인딩 생성 — Light 도메인

2026-10-06 · 소스 구현/정적 검토 단계. 빌드·생성기 실행·런타임 검증은 별도다.

## 1. 범위와 정본

현재 이관은 `LightComponent`의 **13개 함수 슬롯**이다. 기존 API 표의 총 187개
함수 슬롯 가운데 나머지 **174개는 수동 선언·구현**으로 남는다. 이 작업이 전체
스크립트 표면이나 PHASE 9.5의 잔여 트랙을 완료한 것은 아니다.

정본은 `LightComponent.h`의 public getter/writer 선언이다.

```cpp
class [[reflgen::reflect, creator::script_component("Light")]] LightComponent
    : public meta::identity<LightComponent, Component>
{
public:
    [[reflgen::reflect, creator::hide_in_inspector, creator::script_export("Intensity", 3)]]
    float GetIntensity() const { return m_intencity; }

    [[reflgen::reflect, creator::hide_in_inspector, creator::script_export("Intensity", 4)]]
    void SetIntensity(float intensity)
    {
        m_intencity = intensity;
        PublishRenderProxyDirty(ProxyDirty::Payload);
    }
};
```

- `script_component`는 도메인을 선택한다. 타입 선택만으로 모든 멤버를 열지 않는다
- `script_export`의 문자열은 C# 프로퍼티 이름, 정수는 도메인 안의 고정 ABI 슬롯이다
- 0번은 존재 질의다. 메서드의 원본 순서를 바꾸어도 명시한 슬롯은 움직이지 않는다
- 서명은 reflgen의 typed declaration manifest에서 읽는다. 별도의 함수 서명 목록은 만들지 않는다
- `hide_in_inspector`로 getter/writer가 Inspector 호출 버튼으로 추가되지 않게 한다
- private 접근을 위한 reflection friend와 스크립트 노출 허가는 다르다

reflgen은 `d04dd64672ff29bc56993e41a3148bd6f939ab76`에 고정한다. 엔진 포트의
명목 버전은 upstream 1.0.0, port-version은 1이다. 새 릴리스 태그를 가정하지 않는다.

## 2. 생성 경로

SceneRuntime의 reflgen 생성이 선택적 version 1 JSON manifest를 남긴다.
`Tools/ScriptBindings/Generate-ScriptBindings.ps1`이 그 선언과 엔진 정책을 읽어
다음을 같은 구성의 `Build/Generated/ScriptBindings/<키>/`에 만든다.

1. `ScriptLightApi.g.h`: 네이티브 함수 포인터 블록, 계약 fingerprint, 배치 단정
2. `ScriptLightApi.Thunks.g.inc`: 핸들 조회·변환·writer 호출·예외 경계
3. `ScriptLightApi.Fill.g.inc`: 표 초기화
4. `ScriptLightApi.g.cs`: 관리 함수 포인터 블록과 초기화 시 배치 검사
5. `Native.Light.g.cs`: 내부 호출 도우미와 게임 스레드 검사
6. `LightComponent.g.cs`: 기존 이름과 타입을 유지한 공개 프로퍼티
7. `ScriptBindings.contract.json`: 평탄화한 슬롯과 명시적 계약 기록

생성물은 빌드 중간물이다. 소스에 손으로 두 번째 사본을 유지하지 않는다.
생성기에는 Light의 `float`, 색 복사, 두 enum의 정수 경계 변환만 명시적으로
지원한다. 지원하지 않는 선언·속성·타입·중복 슬롯·불일치하는 getter/setter는
출력 전에 거부한다. 다음 도메인은 해당 수명·기본값·변환 정책을 검토한 뒤
같은 경로로 이관한다.

reflgen의 범용 `interop("csharp")` 출력은 이 표에 직접 연결하지 않는다. 그
첫 backend는 호출자가 수명을 보장하는 borrowed 주소를 받는다. 엔진은 자기
세대 핸들 레지스트리와 컴포넌트 조회 규약을 유지해야 한다.

## 3. ABI 보존과 거부

기존 Light 슬롯 85~97을 양쪽 표에서 `ScriptLightApi Light`라는 순차 블록으로
묶었다. 포인터 13개가 차지하는 104바이트는 그대로다. 첫 Light 슬롯은 offset
688, 다음 `Mesh_Exists`는 offset 792다. 기존 187개 함수 포인터의 offset은
바꾸지 않는다.

버전은 33에서 **34**로 올리고, 기존 표 끝 offset 1504에 8바이트 fingerprint를
붙인다. 새 표 크기는 1512바이트다. native `sizeof`·`alignof`·`offsetof` 단정과
관리 측 크기·필드 offset 검사가 같은 경계를 확인한다.

`Native.Bind`는 먼저 연결 상태를 비운 뒤 버전과 크기를 검사한다. 그 다음에만
추가된 fingerprint와 배치를 읽는다. 따라서 구 표를 새 크기로 읽기 전에
구 바이너리를 거부한다. 실패 시 `false`를 반환하는 기존 동작은 유지한다.

fingerprint는 187개 슬롯의 순서·정규화한 경계 서명·호출 규약과 생성된 Light
블록의 핸들/색/enum 배치를 포함한다. **기존 physics/audio 등 모든 POD의 내부
필드 배치까지 자동 이관한 것은 아니다.** 그 영역의 기존 ABI 검사도 계속 필요하다.

## 4. 실행 의미

- 모든 Light 호출은 `Native.Entered()`와 null 함수 포인터 검사를 거친다
- 매 호출마다 `ScriptObjectRegistry.Resolve` 후 `GetComponent<LightComponent>`를
  수행한다. 네이티브 주소를 관리 객체에 캐시하거나 세대 확인을 생략하지 않는다
- 여섯 setter는 기존 writer를 호출한다. `PublishRenderProxyDirty`를 포함한
  writer 본문은 바꾸지 않는다
- 색은 값 복사로 변환한다. enum은 기존 0~2 범위 검사 뒤에만 writer에 전달한다
- 미연결·스레드 거부·객체/컴포넌트 부재의 반환값은 기존과 같다. 색은 흰색,
  스칼라/enum은 0, 존재 질의는 false, setter는 아무 일도 하지 않는다
- 생성된 native 경계는 C++ 예외를 잡고 기록한 뒤 기존 기본값으로 돌아간다.
  로깅 실패도 경계를 넘기지 않는다. 예외 전의 부작용을 되돌리지는 않는다
- CLR 로딩, hot reload 정리, ScriptObjectRegistry 수명, UTF-8 소유권과 다른
  도메인의 호출 구현은 변경하지 않는다

C# 공개 이름은 `Color`, `Intensity`, `Range`, `SpotAngle`, `Type`, `Status`를
유지한다. 사용자는 포인터 표나 ABI 매개변수를 직접 다루지 않는다.

## 5. 빌드와 검증 경계

기존 Windows 소스 빌드 전제인 PowerShell 7과 VS C++ MSBuild를 사용한다.
ScriptCore의 .NET SDK MSBuild는 C++ 프로젝트를 직접 평가할 수 없으므로
launcher가 VS의 MSBuild로 **생성 전용 target**만 요청한다. 전체 native Build나
ClCompile을 요청하지 않는다. manifest와 최종 생성물 사이의 교차 프로세스
경쟁은 생성 단계에만 둔 파일 잠금으로 막는다. 런타임 호출에는 새 잠금이 없다.

구성별 경로, toolchain override, 증분 생성, Clean과 실패 시 잠금 해제 규약은
[빌드 설명](../../Tools/ScriptBindings/BUILD.md)에 있다. 새 포트가 복원되면 기존
reflgen targets 재평가를 위한 재빌드 안내가 적용된다.

현재 단계에서 실행하지 않은 수용 검증:

- 실제 SceneRuntime manifest 생성과 emitter 실행/결정성
- 생성된 C++/C#의 컴파일·정적 분석·트리밍 경고 확인
- 동시 native/managed 빌드, 구성 변경, 생성물 삭제·Clean·실패 복구
- 구/신 바이너리 혼합 거부와 슬롯/배치 변이 검사
- 정상/파괴/부재 핸들, off-thread 호출, Play/Stop/hot reload
- Light 값 왕복뿐 아니라 writer → dirty → 프록시 발행 사슬

생성된 정적 코드와 함수 포인터 호출은 향후 AOT 검토에 재사용할 수 있지만,
NativeAOT backend나 배포 경로를 구현·검증한 것은 아니다. 현재 CoreCLR 호스팅을 유지한다.
