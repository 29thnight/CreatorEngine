# API 48 네이티브 리플렉션 제품 게이트 — 2026-10-29

## 현재 판정

API 48용 실제 Editor·cooked Player 실행 경로를 추가했다. **이 작업 환경에서는 Windows 제품 실행을 하지 않았으며 제품 통과 결과는 아직 없다.** 기존 [API 46/47 검증 기록](FSharpNativeBindingAcceptance20261029.md)은 이전 실행의 증거이며 이 게이트의 실행을 대신하지 않는다.

현재 로컬에서 확인한 범위는 다음과 같다.

| 검사 | 확인한 결과 | 제품 실행 여부 |
|---|---|---|
| `NativeReflectionProduct.Compile.fsproj` | .NET SDK 10.0.403, 현재 reflgen과 공개 `ScriptNative` 프로젝트로 F# 프로브 컴파일 성공 | 컴파일만 수행 |
| `verify-native-reflection-product-results.ps1` | PowerShell 7.4.7에서 결과 소비자 30개 검사와 제품 실행 스크립트 구문 검사 성공 | 합성 결과의 거부 검사 |
| 두 GitHub Actions workflow | YAML 구문 검사 성공 | Actions 실행 전 |
| `verify-fsharp-native-product.ps1` | 실제 제품용 절차 구현 | Windows 실행 대기 |

로컬 컴파일은 Linux에서 `-std=c++20 -D__stdcall= -D__cdecl=`로 생성한 API 선언을 사용했다. Windows 호출 규약 검증은 별도의 Windows 계약 게이트가 맡는다. F# 프로브 자체에는 경고를 오류로 처리하며, 참조하는 기존 `ScriptNative` 프로젝트의 경고 63개는 그대로 보고됐다.

## 실제 제품 게이트가 요구하는 증거

[`verify-fsharp-native-product.ps1`](../../Tools/regression/verify-fsharp-native-product.ps1)은 검증한 엔진 배포물을 입력으로 받는다. 배포물의 구성·소스 커밋·native API가 현재 체크아웃과 일치하고 저장소 Editor 실행 파일이 배포본과 같은지 먼저 확인한다. 별도 작업 디렉터리에 기존 캐릭터 물리 장면, F# 프로브, 직접 생성한 PCM WAV와 셰이더를 구성한다.

Editor와 Player는 각각 자신이 시작한 프로세스의 PID와 명령 서비스 endpoint만 사용한다. 성공 줄은 정확히 한 개여야 하며 종료 뒤 전체 출력을 다시 읽어 늦게 도착한 실패·중복 결과도 거부한다.

1. 실제 네이티브 카탈로그의 컴포넌트 이름을 `ComponentTypeUUID.h`의 전체 목록과 대조한다. `ScriptComponent`와 `InvalidScriptComponent`, 고유 타입 ID, 컴포넌트보다 큰 도달 가능 타입 집합을 확인한다.
2. F# 생성 타입으로 Light 스칼라·색, 중첩 렌더 프로필, Decal 필드, Unicode 상태 이름, nullable borrowed 결과, Character 정의와 Sound 설정 값 반환을 읽고 쓴다.
3. 실제 고정 틱에서 실행 순서와 60 Hz 간격을 확인하고 캐릭터 상태 조회·목표 속도 설정 뒤 월드 X 위치가 0.1보다 크게 변했는지 확인한다. 30~180 틱 이내에 끝나야 한다.
4. Sound의 타입 지정 재생·일시정지·재개·정지, GC 이후 값 핸들, 비동기 종료 알림을 확인한다. Editor와 Player 종료 시의 `audio.host` 이벤트는 Device 모드, 실제 출력 콜백, 백엔드 실패 0을 요구한다. Null·DegradedDevice·Offline은 통과할 수 없다.
5. Editor에서 Decal을 제거하고 다시 추가한 뒤 이전 핸들이 `stale_component`로 거부되는지 확인한다. Play/Stop 커밋과 정상 종료도 요구한다.
6. 같은 장면을 쿠킹·패키징하고 native presenter에 실제 게임 프레임을 제출하는 패키지 smoke를 실행한다. 이어서 별도 Player를 실행해 F# 프로브의 완료를 기다린다. 짧은 렌더 smoke가 고정 틱 검증을 대신하지 않는다.
7. 각 호스트의 순서가 있는 runtime journal, 렌더 백엔드 큐 정리와 정상 종료를 확인한다. Player의 authoring 텍스트 파서 호출은 0이어야 한다. Player 실행 전후의 패키지 파일 목록·해시도 같아야 한다.

음성 장치 콜백과 논리 재생 상태를 확인하는 검사다. 사람에게 들리는 파형의 품질, 모든 렌더 효과의 픽셀 정확성, 모든 카탈로그 멤버의 실제 사용까지 보장하지 않는다. 카탈로그 전체성 검사와 대표 멤버의 실행 검사는 다른 범위를 측정한다.

Editor 관리 스크립트 네 파일은 먼저 백업한다. 성공·실패 모두 소유한 Editor를 종료한 뒤 원본을 복원하고 해시를 확인한다. 엔진 배포물 자체는 변경하지 않는다.

## Windows 실행 조건

[`native-reflection-product.yml`](../../.github/workflows/native-reflection-product.yml)은 수동 실행 전용이다. 자동 PR 실행으로 신뢰된 워크스테이션을 사용하지 않는다. 기본 runner label은 `["self-hosted","Windows","X64"]`이며 실행 시 변경할 수 있다.

- 실제 DX12 장치와 출력 가능한 오디오 장치를 가진 Windows 세션.
- Visual Studio 2026 C++ v145, Windows SDK, .NET 10 SDK, PowerShell 7.
- 사용자 MSBuild에 통합된 vcpkg. 체크아웃은 엔진 `vcpkg.json`의 `builtin-baseline` 커밋과 같아야 한다. 게이트는 프로젝트 빌드 전에 매니페스트 의존성을 설치한다.
- `GITHUB_WORKSPACE/FSharp_Tool_Pakage_for_CreatorEngine`의 기존 **비공개** F# 도구 체크아웃. 기본 고정 커밋은 `dcfa051e1354a6caa3ab4389b168429f73a2342e`이며 workflow 입력으로 정확한 다른 커밋을 지정할 수 있다. 작업 트리가 깨끗해야 한다.

엔진 checkout은 `GITHUB_WORKSPACE/CreatorEngine`에 배치되므로 사전 설치한 형제 도구 저장소와 분리된다. 공개 엔진 저장소의 `GITHUB_TOKEN`으로 비공개 도구를 가져오지 않으며 새 비밀 키도 가정하지 않는다. 필수 도구가 없거나 오디오가 대체 모드로 내려가면 게이트가 실패한다. 조건부 건너뛰기로 성공을 만들지 않는다.

workflow는 BuildTool로 실제 Editor·Player·쿠킹 도구를 빌드하고 불변 배포물을 만든 뒤 제품 게이트를 실행한다. 업로드 목록은 최상위 진단 로그·JSON 결과로 한정한다. 비공개 도구 소스, 배포물, 백업 관리 어셈블리, 임시 command-service token은 업로드하지 않는다.

준비된 개발 환경에서 직접 실행할 수도 있다.

```powershell
./Tools/regression/verify-fsharp-native-product.ps1 `
    -Configuration Release `
    -EngineDistribution C:/CreatorEngine/Build/Distributions/<verified-distribution>
```

`-Work`를 지정하면 존재하지 않는 새 디렉터리여야 한다. 성공 시 해당 디렉터리의 `result.json`은 `NATIVE_REFLECTION_PRODUCT_OK`, 실제 PID별 프로브 결과, 사용한 소스 커밋·API, 오디오 장치 진단을 기록한다. 실패 시 남아 있는 출력은 원인 분석용이며 통과 증거가 아니다.

## 공개 Windows 계약 CI와의 구분

기존 [`native-reflection-contracts.yml`](../../.github/workflows/native-reflection-contracts.yml)의 Debug·Release 작업은 네이티브 서비스 fixture와 F# 계약, 실제 엔진 헤더 생성·컴파일을 실행한다. 여기에 **제품에서 사용할 동일 F# 소스의 컴파일**과 결과 소비자 검사를 추가했다. 이 작업은 비공개 도구 없이 공개 `ScriptNative`를 참조하며 실제 Editor/Player를 링크하거나 실행했다고 주장하지 않는다.

비공개 의존성과 장치가 준비된 runner에서 새 제품 workflow가 실제로 끝나고 `NATIVE_REFLECTION_PRODUCT_OK`를 남겨야 API 48 제품 검증 완료로 기록할 수 있다.
