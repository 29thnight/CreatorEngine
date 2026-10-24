# TR2 — render/display·jitter 및 멀티뷰 품질 수용

2026-10-23 · master `0509753a380866e8dd96bc648fd71c5c5d8792e2`와 보존된 로컬 변경 · DX12 Editor · RTX 4070 Ti

TR2의 남은 멀티뷰·전체 route/해상도 품질 검증을 완료했다. 기존 temporal snapshot, 성공한 제출에 따른 history, SSGI reset, command service와 GPU attachment 캡처를 재사용했다. **TR2 4/4·5인일 수용**이다.

## 구현과 원인 수정

- `sdk-routes` / `sdk-routes-solo`가 실제 Game view의 7개 route와 별도 Editor companion view를 실행한다. static·skinned·instanced·decal·alpha·world-space UI·meshlet을 같은 authored 입력으로 검사한다. render/display 크기, 카메라, 품질, 정확한 34회 warmup과 Halton 단계, 현재/이전 제출 소유권을 캡처에 남긴다.
- fixture를 현재 METEOR cooked geometry 계약에 맞췄다. 모든 불투명 geometry에 meshlet topology를 만들고, 중복 blob을 공유하며 geometry locator에 thin metadata digest를 사용한다.
- cooked loader의 진단 문자열은 validator 실행 이후 전달한다. 기존 단일 표현식은 validator가 문자열을 재할당하기 전에 `c_str()`를 평가할 수 있었다. meshlet 계약 검사는 정확한 오류 문자열까지 확인한다.
- full CEMF3의 DXIL/SPIR-V cook에서 필요한 descriptor-heap texture source capability를 SceneCompiler가 명시하고 shader cache key에 반영한다. 경고를 오류로 처리하는 정책을 유지한다.
- world-space UI fixture의 transient cooked texture를 기존 DeserializeTexture 경로에 바인딩했다. authoring path가 없는 texture를 Load로 처리하던 fixture의 빈 출력이 사라졌다.
- fixture 사이에는 완료된 immutable prepared asset만 재사용한다. compiler·platform·ABI·shader 원본 해시를 대조하며, 매 fixture마다 원본 asset bytes와 해시 증거를 다시 기록한다.

- SDK route마다 기준 입력의 첫 제출에서 adapter를 종료·재생성한다. private resource·counter를 포함한 시작 상태를 통일한 뒤 정확히 34회 warmup을 수행한다. manifest의 `historyStart`도 판정 도구가 확인한다.
- cold material cook과 GPU capture에 각각 5분 제한을 둔다. 이전에는 두 단계가 한 제한을 공유해 큰 해상도로 바로 시작한 Debug 검사의 마지막 capture가 취소됐다.

## 독립 route 초기화와 FSR 노출 연산

각 route의 기준 입력이 준비된 첫 실제 제출에서 SDK context를 새로 만들고 jitter/history를 초기화한다. 이후 34회의 warmup과 current/control history는 그대로 이어진다. 이 정책은 명시적인 SDK fixture에만 적용된다. 카메라 전환 후 binding을 기다리며 제출된 프레임이 독립 비교의 시작 상태에 섞이지 않게 한다.

초기화만으로는 Debug의 큰 해상도에서 FSR 출력 차이가 남았다. 37회 전체 SDK 입력을 읽어 비교한 결과, 색·깊이·모션·마스크와 내부 상수가 모두 같았다. 첫 차이는 자동 노출의 로그 휘도 이력에서 부동소수점 한 단계로 나타났고 이후 FP16 누적 색상과 display 출력에 전파됐다. 원래 휘도 reduction 값도 같았다. 매 프레임 GPU 완료를 기다리는 직접 재생은 8회씩 세 실험에서 최종 출력이 일치했다.

자동 노출 누적 변수에 `precise`를 지정해 계산 순서를 보존했다. 임시 GPU 입력·내부 버퍼 계측을 제거한 최종 Debug/Release 빌드에서 전체 행렬과 독립 solo 반복 비교를 1e-6 기준 그대로 통과했다. 고정 SDK에 적용되는 변경은 별도 patch로 보존한다. 진단 비교 기록 · 로컬 기록, 사이트 미포함 (`Artifacts/tr2-20261023/fsr-diagnostic-summary.json`).

SPD memory barrier·전체 mip 초기화·reset 시점 정렬 실험은 차이를 해결하지 못했다. 이 실험들은 최종 SDK에서 제거했다. SDK 파일 복원 시 이전 수정 시각이 남아 셰이더 재생성이 생략된 구간도 확인했으며, 최종 빌드에서는 원본과 검증한 precision patch로 shader blob과 라이브러리를 다시 생성했다. 실패 결과와 원본 캡처는 Artifacts에 보존했다.

## 표시 셰이더 정밀도와 추가 입력 검증

FSR 노출 수정을 적용한 뒤, Release FSR Balanced와 Debug DLSS Native AA에서 HDR 출력은 같지만 표시 출력 일부 성분이 1/255씩 달랐다. bloom 최종 텍스처와 포스트 설정을 기존 캡처에 추가한 결과, 단독·교차·단독 반복의 28개 route/단계에서 HDR·bloom·설정이 모두 같고 표시값만 달랐다. 실제 생성된 표시 셰이더를 같은 입력으로 직접 128회씩 재생한 RGBA32F·RGBA8 출력은 각각 전부 일치했다. 추가 입력 비교 · 로컬 기록, 사이트 미포함 (`Artifacts/tr2-20261023/post-bloom-trace-comparison.json`).

8비트 표시 코드를 명시적으로 반올림하는 후보는 Release 집중 검사에서 통과했지만 Debug의 독립 단독 반복에서 다시 실패했다. 이 후보는 제거했다. 최종 변경은 `EnhancedPostChainPass::CompilePostShader`에서 Uber·UberHDR 두 톤매핑 셰이더에 기존 `strictMath` 옵션을 적용하는 것이다. 해당 옵션은 shader cache identity에 포함되며 Slang의 `-fp-mode precise`로 전달된다.

bloom·포스트 설정 캡처는 동일 입력을 검증하는 도구로 유지했다. 일반 렌더링에는 추가 readback을 넣지 않고 명시적 캡처에서만 실행한다. 최종 단독/교차 검사는 기존 10개 이미지에 bloom을 더한 11개 전체 이미지를 비교하고 포스트 설정도 일치해야 통과한다. 기존 `dx12.post`의 bloom·tone map·vignette·FXAA 검사도 함께 실행했다. 픽셀 비교 허용 오차는 1e-6을 그대로 유지했다.

## SDK 설치와 재현

SDK는 `ThirdParty/Temporal`에 설치했다. FidelityFX SDK v1.1.4(FSR 3.1.4), Streamline v2.14.1, XeSS v3.0.2를 고정했다. 버전·revision·원본 archive 및 배포 DLL 해시는 [SDKVersions.json](https://github.com/29thnight/CreatorEngine/blob/7c59b8f8b99ff2c1b26cd72fa9b1a0764734f0ff/ThirdParty/Temporal/SDKVersions.json)에 있다.

검증 빌드는 `Validation.props`를 opt-in으로 적용한다. 일반 엔진 빌드의 SDK 기본 옵션은 유지한다. 외부 SDK 빌드는 별도 Directory.Build 파일로 엔진 module/reflection 설정을 격리했다. 구성별 20개 temporal runtime DLL을 Editor/Temporal에 배포했다. [설치·빌드 안내](https://github.com/29thnight/CreatorEngine/blob/7c59b8f8b99ff2c1b26cd72fa9b1a0764734f0ff/ThirdParty/Temporal/README.md).

```powershell
& ThirdParty/Temporal/Build-Validation.ps1 -OutputDirectory Artifacts/tr2-rebuild
& Tools/regression/verify-tr2-history.ps1 -Configuration Debug -OutputDirectory Artifacts/tr2-history-debug -IncludePostChain
& Tools/regression/verify-tr2-resolution.ps1 -Configuration Debug -OutputDirectory Artifacts/tr2-matrix-debug -Resolutions 1600x900,960x640,1280x720 -RepeatFirstSoloControl
# Release도 같은 두 회귀 도구의 Configuration을 Release로 실행한다.
# 반복 대조군 없는 matrix만 같은 바이너리·출력 경로에 -Resume을 사용할 수 있다.
```

Resume은 이전 프로세스의 validation 0 기록과 완료된 단독/교차 oracle·isolation 세트가 함께 있을 때 재사용한다. 미완료 세트는 보존 디렉토리로 이동한 뒤 단독/교차를 함께 다시 만든다. 바이너리 해시가 다르면 재개를 거부한다.

최종 Debug 행렬은 동일 바이너리로 해상도 그룹을 나눠 실행했다. 초기 9조건의 모든 캡처가 validation 0 확인 전에 완료됐음을 대조한 뒤 정상 종료하고, 큰·작은 해상도 그룹을 Resume으로 이어갔다. 중간 해상도는 별도 프로세스에서 실행했다. 미완료 조건은 재사용하지 않았다. 재사용 증거 대조 · 로컬 기록, 사이트 미포함 (`Artifacts/tr2-20261023/debug-resume-reuse-audit.json`).

## Debug / Release 결과

| 구성 | native fixture | SDK 조건 / fixture | GPU 캡처 | 전체 이미지 비교 | 단독/교차 최대 오차 | SDK 내부 최대 상대 색상 오차 |
|---|---:|---:|---:|---:|---:|---:|
| Debug | 3 | 45 / 90 | 1,316 | 6,930 | 0.0 | 0.6414% |
| Release | 3 | 45 / 90 | 1,316 | 6,930 | 0.0 | 0.6414% |

각 구성의 SDK 행렬은 **3 provider × 5 quality × 3 해상도**다. quality는 Native AA·Quality·Balanced·Performance·Ultra Performance이며 매 조건에 단독/교차 view를 각각 실행했다. host 창 960×640·1280×720·1600×900에 대응하는 실제 display extent는 캡처 manifest와 [세부 결과 JSON](TemporalTr2Resolution20261023.json)에 기록했다. 창 크기를 실제 viewport 크기로 대체해 기록하지 않는다.

- Debug/Release 최종 빌드 오류 0. Debug 경고 25개, Release 경고 33개가 기록됐다. 구체적인 경고 종류는 링크한 빌드 로그에 보존했다.
- 구성별 temporal history 계약 **2,171검사**, meshlet 계약 **73검사** 통과.
- 구성별 `dx12.post` 포스트 체인 회귀 통과.
- 구성별 SSGI GPU **16비교 / 999,424 scalar**, 최대 오차 **0**. view state·reset·실패 복구 검증을 같은 최종 바이너리로 재실행했다.
- matrix 및 history/SSGI의 DX12 validation messages·problems·dropped 모두 **0**, 프로세스 정상 종료.
- 독립 motion oracle **26개**, resolution oracle **8개** 단위 검사 통과.

최종 행렬마다 큰 해상도의 FSR Native AA에서 독립 solo fixture를 한 번 더 실행했다. 구성별 14캡처·154개 전체 이미지 반복 비교가 추가됐고 최대 차이는 0이다. 최종 행렬 증거는 **2,632캡처·14,168개 전체 이미지 비교**다. 이전 실패·시간 초과·수정 전 대조군은 보존하되 이 합계에 포함하지 않았다.

## 픽셀 판정

원본 asset·shader·pose·camera·render/display·jitter를 먼저 대조하고 color/depth/motion의 route 소유권과 negative control을 검사한다. 실제 SDK HDR 입력과 reactive·transparency·responsive mask도 함께 대조한다. 단독/교차 view는 7 route × 2 capture 단계 × 11 attachment의 **모든 픽셀**을 비교한다. 허용 오차는 1e-6이며 결과는 전부 0이다.

SDK 내부 색상은 source silhouette·binary alpha contour에서 최소 3 render pixel 떨어진 authored probe를 사용한다. 각 instance마다 최소 3개를 요구하며 상대 색상 오차 10% 및 출력 contrast 기준을 적용한다. DLSS Performance에서 alpha contour로부터 0.47 input pixel 떨어진 probe가 내부로 분류되어 11.17%를 기록했다. 실제 alpha texture contour와 inverse UV Jacobian으로 경계 거리를 계산하도록 고쳤다. 경계의 입력 ownership 검사와 전체 이미지 멀티뷰 비교는 유지한다. 출력 색상에 따라 probe를 선택하지 않는다.

파일 해석과 전체 이미지 비교에는 설치된 NumPy를 사용하고, 미설치 환경에서는 기존 scalar 경로로 실행한다. float64 차이 계산은 Python float 계산과 같으며 scalar/vectorized의 84개 비교 결과 일치와 NaN/무한대 거부를 확인했다.

## 증거와 후속 소유 범위

- 소스·shader·runtime DLL·바이너리 해시 · 로컬 기록, 사이트 미포함 (`Artifacts/tr2-20261023/source-freeze.json`)
- Debug 큰·작은 해상도 · 로컬 기록, 사이트 미포함 (`Artifacts/tr2-20261023/stable-matrix-Debug/matrix.json`) · Debug 중간 해상도 · 로컬 기록, 사이트 미포함 (`Artifacts/tr2-20261023/stable-matrix-Debug-medium/matrix.json`) · Release 행렬 · 로컬 기록, 사이트 미포함 (`Artifacts/tr2-20261023/stable-matrix-Release/matrix.json`)
- Debug 최종 빌드 · 로컬 기록, 사이트 미포함 (`Artifacts/tr2-20261023/stable-build-Debug/build-Debug.log`) · Release 최종 빌드 · 로컬 기록, 사이트 미포함 (`Artifacts/tr2-20261023/stable-build-Release/build-Release.log`)
- Debug history · 로컬 기록, 사이트 미포함 (`Artifacts/tr2-20261023/stable-history-Debug/result.json`) · Release history · 로컬 기록, 사이트 미포함 (`Artifacts/tr2-20261023/stable-history-Release/result.json`)
- Debug SSGI · 로컬 기록, 사이트 미포함 (`Artifacts/tr2-20261023/stable-ssgi-Debug/results.json`) · Release SSGI · 로컬 기록, 사이트 미포함 (`Artifacts/tr2-20261023/stable-ssgi-Release/results.json`)

이번 수용은 DX12 실제 Editor의 제어된 7-route·SDK 품질·해상도·멀티뷰 행렬이다. 복잡 제품 장면의 장기 잔상·disocclusion, 전체 프레임 비용, 장치별 지원·배포·폴백은 TU0~4에서 계속 추적한다. FG와 Player 물리 표시 cadence/latency는 각 FG 항목에서 추적한다. SPIR-V는 CEMF3 cook으로 생성했고 실행 검증은 DX12에서 수행했다.
