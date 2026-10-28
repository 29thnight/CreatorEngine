# F# 네이티브 바인딩 검증 기록 — 2026-10-29

> 후속 API 47 작업: Bone의 pinned/인덱스와 Decal의 애니메이션 필드가 추가됐다. Debug·Release 네이티브 테이블 검사와 Debug Headless 회귀가 통과했고, Debug Editor에서 Decal의 값 왕복을 확인했다. Bone의 실제 모델 장면과 나머지 컴포넌트 멤버는 이 결과로 검증되지 않았다. 아래 API 46 결과는 이전 실행 기록이다.

## 계약과 제품 경로

- 네이티브와 관리 API는 버전 46이며 테이블 순서는 205개로 일치한다. reflgen 1.2.0의 F# 생성 결과와 네이티브 ABI 지문을 함께 검사한다.
- 기본 Editor 저작, Player, Headless, 배포 도구는 `CreatorEngine.NativeBindings.dll`과 F# 실행기를 사용한다. 별도 비교 빌드에서만 `CreatorLegacyScriptBaseline=true`와 `CREATOR_FSHARP_NATIVE=0`으로 이전 경로를 선택한다.
- `AudioPlaySettings()`의 CLR 기본 생성자는 빌드 시 생성된다. 생성 도구는 배포물이나 런타임 바인딩 계층이 아니다.
- `ClrHost`의 엔티티·트랜스폼·입력 바인딩은 별도 등록 단위로 분리했다. 바인딩 등록 표는 명시적인 API 46 ABI를 사용한다.

## 실행 증거

검증 출력은 저장소의 `Build/Verification` 아래에 있다. 이 기록의 각 경로는 해당 실행의 원본 결과를 가리킨다.

- F# 스크립트 소스 57개 컴파일: `NativeCorpus-c15d02340dd14cca85db400e07f14483` (Debug), `NativeCorpus-7a09b3a221af4866a0b5ac271995b0e7` (Release). 기존의 이름 붙인 `TextureAssetVariant(Role = 1u)`도 새 바인딩에서 같은 값과 기본값으로 생성한다.
- Editor 자동 저작·실패 복구·재로드: `Build/AutomaticScripts-Debug-0c58be81/result.json` (107개), `Build/AutomaticScripts-Release-c638fb1a/result.json` (94개).
- 실제 cooked Player와 API 46 고정 틱: `FSharp/Player-287f2912ab084783bf3a290d84fa6eae/result.json` (Debug), `FSharp/Player-7cb1bb1e4af547e7be75c3a16a3acf01/result.json` (Release).
- Headless 물리·고정 틱·장면 회귀: `Headless/Run-3db6303884f64a2188a41ea2a8179a2f/result.json` (Debug), `Headless/Run-785762ff2a934f9eb64ce85c164bd07a/result.json` (Release). 오래된 선택 설정을 넣은 Headless 이동 검사는 `ScriptNativeHeadless-5a14ff50822f4610aa278281051dd1d4/report.json`이다.
- 실제 오디오 Player: `CliConsumers/Fixtures-e3592f7e31e643a49b40ab16e429ae19/Audio/package.log`; 100회 재생·완료와 MP3/FLAC, 스트리밍, 소유 범위를 확인했다.
- 실제 텍스처 AssetSet Player: `FSharp/Player-9edf5076fa7249ff97d80003ca54534d/result.json` (Debug), `FSharp/Player-9c8d918fefde4c3e856e5014aa9e0a96/result.json` (Release); F#에서 512×512 텍스처를 요청하고 결과·설명을 읽은 뒤 요청과 핸들을 해제했다. 소스 텍스처만 복사한 최초 시도는 `NotMounted`로 실패했으며, AssetSet을 빌드·활성화한 뒤 통과했다.
- `FSharp/Player-4a0106f87b9048b3aab238e878e35716/result.json` (Debug)과 `FSharp/Player-ee6c4c7a46d74793b709c041f073f25b/result.json` (Release)은 이름 붙인 텍스처 옵션을 사용하고 작업 스레드에서 핸들을 해제한 뒤 게임 틱에서 정리 큐를 처리하는 제품 경로를 추가로 실행했다.
- BuildTool 회귀 189개와 API 테이블 순서 검사가 통과했다. 메타데이터 대조에서 공통 공개 타입의 일반 메서드 이름과 인자 개수는 일치한다. 원자료 `scriptnative-surface-audit.json`은 필드·속성, 생성자, 연산자, 호스트 전용 ABI 타입의 표현 차이도 포함하며 매개변수 형식이나 동작의 동등성을 단독으로 보장하지 않는다.
- 이전 C# 네이티브 컴포넌트의 공개 무인자 생성자는 소유 핸들이 없는 wrapper를 만들었다. 새 F# wrapper는 엔진이 확인한 소유 핸들로 생성하며, 현재 `GameScripts`와 `Dynamic_CPP/Assets/Script`에서 이전 무인자 생성자를 직접 호출하는 곳은 없다.
- Debug·Release `verify-script-native-table.ps1`은 10가지 에셋 종류의 안정 ID 왕복과 5회 연속 재로드 후 이전 collectible 컨텍스트 수거를 확인했다.
- `NativeSceneWrappers-107b110b8e074d2798a4fead7b9fc833`은 실제 Debug Editor에서 F# 카메라 15개 검사와 라이트 값 왕복·dirty 발행·프록시 큐잉을 확인했다. 렌더 스레드의 최종 라이트 적용은 이 검사 범위에 포함하지 않는다.
- `NativeBt-d6dccbb763634c398903e4695fc10202/test.log`는 별도 프로젝트의 실제 Editor에서 F# 행동 트리 3개 생성, 저작 블랙보드와 시퀀스 완료, 틱 배치, 정지와 씬 교체 후 정리를 확인했다.
- `NativeUi-e25f5581da2841a0bba48efa30411bc6`은 별도 프로젝트의 실제 Editor에서 F# UI 사각 영역·이미지 API의 7개 왕복 검사를 통과했다. 이 장면에는 카메라가 없어 UI 프로브의 카메라 검사는 건너뛰며, 카메라는 위의 별도 검사에서 확인했다.
- `NativeUi-9cedad25e7294f7b9f780926f0b7b5ec`은 실제 Editor에서 F# Animator wrapper 조회, 없는 파라미터 기본값, 레이어와 정지 호출을 검사했다. 컨트롤러 파라미터 왕복은 이 빈 Animator 장면의 범위에 없다.
- 명시적 비교 빌드 `LegacyComparisonDistribution/local-0.0.0.0-win-x64-Debug-131a71ce-2c45-491f-bba7-42f6cb0d1ef0`와 그 Player `FSharp/Player-3283ae24aafc4362bab148387cc2a067/result.json`도 실행했다. 일반 제품 빌드와 별도다.

## 성능 증거와 범위

실제 Player의 동일 장면에서 이전 경로와 F# 경로를 각각 네 번 교차 실행했다. 소스와 실행 파일 해시, 원본 기록, 프레임·고정 틱 분포는 다음에 있다.

64회 반복 장면의 후속 회귀 예산은 측정 전 고정 틱 p95 중앙값의 증가분 25µs 이하로 정했다. 이는 60 Hz 틱 간격 약 16.67ms의 0.15%에 해당하며 이 한 스크립트 입력에만 적용한다. 64개 스크립트나 전체 게임의 성능 예산은 아니다. 호출 단위 반복 구간의 추가 관리 할당은 0이어야 한다.

- Release: `ScriptPlayerFrames-64d43c4fd75f41f6a819e7b7900b9a6c/comparison.json`. 고정 틱 평균은 이전 10.4–11.0µs, F# 13.0–15.3µs다.
- Debug: `ScriptPlayerFrames-cebbdc1dad6a4687bdd3200395b7cfa3/comparison.json`. 고정 틱 평균은 이전 28.3–31.7µs, F# 30.3–32.4µs다.
- 프레임 경계는 두 구성 모두 약 60 Hz로 제한되므로 해당 결과만으로 무제한 처리량의 동등성을 판정하지 않는다. 기록에는 각 실행의 p95와 실제 렌더 스레드 분포도 있다.
- Release 호출 단위 실험 `ScriptBindingPerformance-580af5f40e894b7689ae27c667a41477/results.csv`에서 상태 조회는 이전 약 13.7ns, F# 약 22.6ns이고 캐릭터 읽기·속도 설정 두 호출은 각각 약 33.0ns와 55.0ns다. 반복 구간 할당은 0이다. 이 차이만으로 실제 고정 틱 차이를 설명할 수는 없다.
- 구간별 계측을 넣은 Release Player `ScriptPlayerFrames-e2f7eeff540d438e968e6ffd105b6b8b/comparison.json`에서는 상태 조회 구간이 이전 0.58–0.75µs, F# 1.50–2.21µs였다. 이 구간에는 각 언어의 프로파일 종료 호출도 포함된다. 상태 조회를 직접 호출로 바꾼 실험 `ScriptPlayerFrames-72a8d715fb4e4889a9bbd09ee93836d3/comparison.json`에서는 전체 틱의 개선이 확인되지 않아 해당 실험은 되돌렸다.
- 빈 프로파일 구간을 포함한 대조 `ScriptPlayerFrames-3e06a86d0a4f464a8da06707c8eaf741/comparison.json`에서 빈 구간 자체는 이전 0.18–0.21µs, F# 0.74–0.85µs였다. 계측된 상태 조회 구간의 차이를 API 호출 자체의 비용으로 해석하지 않는다.
- 고정 틱마다 상태 조회와 캐릭터 읽기·쓰기 호출을 64회 수행한 동일 Player 장면 `ScriptPlayerFrames-1d118e0d152c47a380975a886c955776/comparison.json`에서 고정 틱 평균은 이전 39.3–42.3µs, F# 42.1–51.8µs다. 이는 64개 스크립트가 아니라 한 스크립트의 반복 호출 측정이다. 프레임은 두 경로 모두 약 60 Hz로 제한됐다.
- 사전 예산을 정한 뒤 다시 실행한 Release `ScriptPlayerFrames-df002e17069541e8ad24eb3d9192814a/comparison.json`의 고정 틱 p95 중앙값은 이전 78.05µs, F# 85.55µs다. 증가분 7.50µs가 이 입력의 25µs 예산을 통과했다.
- 같은 Debug 입력 `ScriptPlayerFrames-265c0b2de73c44b9a615c0518dccf4e2/comparison.json`의 고정 틱 p95 중앙값은 이전 267.95µs, F# 283.45µs다. 증가분 15.50µs가 같은 예산을 통과했다.

위 성능 기록은 비교용 이전 경로를 선택할 수 있던 동일 Player 이진 파일을 사용했다. 이후 일반 제품에서 비교 경로 선택을 컴파일 시 제외했으며, 그 제품의 기능 검증은 별도로 수행한다. 고정 틱 비용 차이의 나머지 원인과 더 많은 스크립트가 있는 장면의 성능 예산은 아직 확정하지 않았다.
