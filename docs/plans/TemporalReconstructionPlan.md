# 시간축 재구성·프레임 생성의 현재 계약

기준: master `493536e300e274cbf59989cdd62b20310c8ae64f`의 소스와 실제 호출 배선. 이번 개정에서는 빌드·테스트·GPU 실행을 하지 않았다. 구현 존재와 실행 수용을 구분한다.

## 현재 연결과 목표 경계

시간축 업스케일(TU)은 라이브 그래프에 연결되어 있다. Temporal.Inputs와 SpriteMotion이 입력을 만들고 Temporal.Upscale이 tone mapping 전 HDR을 display 해상도로 재구성한다. frame generation(FG)은 렌더러의 새 시뮬레이션 프레임 생산이 아니라 Player presentation 경로의 보간 기능이다. renderer provenance의 frameGenerator는 None이며 실제 FG 상태는 presenter에서 관찰해야 한다.

TemporalProvider의 Fsr, Dlss, XeSS enum이나 adapter 파일 존재는 해당 실행 장치에서 기능이 활성화되었다는 뜻이 아니다. optional SDK 빌드, runtime 로드, API 지원, capability, context 생성, 입력 유효성, 실제 dispatch와 제출을 모두 구분한다.

## 입력 의미

TemporalFrame의 카메라 행렬은 jitter가 없는 row-major 값이다. jitter는 render 해상도의 pixel 단위이고 motion은 현재 픽셀에서 이전 픽셀로 향하며 camera motion을 포함하고 jitter는 제외한다. 원점은 왼쪽 위, X는 오른쪽, Y는 아래다. depth와 projection 의미, near/far, FOV, 실제 render/display extent도 같은 프레임의 값이어야 한다.

Upscale 입력은 pre-tone-map linear HDR, depth, motion과 선택적인 exposure·reactive·transparency·responsive mask다. responsive와 reactive는 다른 의미다. 결과는 UI 이전의 display 해상도 HDR이다. FG에는 HUD 없는 색과 별도 UI, depth, motion, 실제 소비 종료까지 자원을 잡는 lifetimeToken이 필요하다.

## 뷰별 history와 실패

EnhancedTemporalViewState는 논리 view, scene epoch, history revision, 마지막 제출 camera/geometry/decal/sprite를 보관한다. Begin은 첫 프레임, 비단조 frameId, 뷰·씬·렌즈/투영·크기·재구성 설정·provider·명시적 reset 변화에서 history를 무효화한다. jitter와 이전 입력은 준비만으로 진전하지 않는다.

Commit은 그래프 기록·제출 성공 뒤 실행된다. 실패하거나 pending 준비로 보류된 프레임은 성공한 이전 프레임으로 게시하지 않는다. PublishSubmitted의 selectedUpscaler와 activeUpscaler는 다르며 실제 temporal AA 적용 관찰을 통해 active를 정한다.

TemporalUpscalerHost는 요청 provider를 시도하고 별도 후보 FSR을 시도한 다음 native 해상도 경로를 유지한다. orthographic는 ProjectionUnsupported를 기록하고 native rendering을 유지한다. SDK 실패를 저해상도 텍스처의 성공한 재구성으로 표시하지 않는다. context 해제에는 owner의 실제 retirement가 필요하며 CPU 제출 ticket만으로 SDK 자원을 해제하지 않는다.

## API별 지원 경계

FSR은 DX12/Vulkan 빌드 옵션을 분리한다. DLSS는 현재 host에서 DX12 session을 사용하며 Vulkan은 BackendUnsupported다. XeSS TU는 DX12와 optional Vulkan 경로가 있고 XeSS FG의 Vulkan 상태는 unsupported다. FG target은 PlayerSwapchain이며 EditorViewport 금지 상태가 명시되어 있다. 정확한 장치 지원은 runtime snapshot과 adapter query로 판단한다.

저지연 모드는 TU·FG와 독립 설정이다. marker/sleep SDK 호출 성공 카운터를 실제 입력 지연 측정으로 표시하지 않는다. NativeAA, Quality, Balanced, Performance, UltraPerformance는 품질 선택 값이며 모든 provider·장치의 동일 지원을 뜻하지 않는다.

## 설정과 관찰

TemporalRuntimeControl은 제품 설정과 환경 입력을 읽는다. CREATOR_TEMPORAL_UPSCALER, FRAME_GENERATOR, QUALITY, ENABLED, INTERPOLATED_FRAME_COUNT와 runtime directory/project id 등의 입력이 있다. 설정 요청 generation, renderer observed generation과 presenter observed 상태를 분리한다. 요청을 받았다는 응답만으로 전환 완료를 판단하지 않는다.

진단·golden 비교는 native capture exclusion과 TemporalMeasurementProvenance를 확인한다. real/generated/unknown, source frame, view, scene epoch, 실제 해상도와 reconstruction 상태가 맞지 않는 표본은 성공 비교에서 제외한다. 캡처 프레임의 readback 비용을 normal-frame 성능에 합치지 않는다.

## 남아 있는 제품 수용

1. SDK를 포함한 실제 바이너리·runtime hash와 장치를 기록하고 provider별 지원 상태를 확인한다
2. startup과 warmed 상태를 나눠 native↔provider, 모든 지원 품질, resize, FOV·near/far, camera cut·장면 교체를 검사한다
3. 정적·스킨·인스턴스·alpha·decal·sprite motion, 첫 이동 disocclusion과 history reset을 독립 reference로 검증한다
4. capability와 dispatch 실패 뒤 요청 결과·fallback·history·재시도 범위가 맞는지 검사한다
5. Player FG의 UI 분리, generated 표시 cadence, resize·비활성화·shutdown과 마지막 GPU 소비 수명을 확인한다
6. CPU/GPU 비용과 실제 latency를 별도 측정하고 Vulkan 및 여러 vendor 장치에서 parity를 판정한다

날짜별 DLSS·TR0 기록은 그 주제의 호환 문서로 남긴다. 로컬 Artifacts의 존재나 과거 성공 수치를 현재 커밋의 실행 증거로 재사용하지 않는다. 전체 PHASE 완료율과 과거 인일 합계는 코드 감사만으로 갱신할 수 없다.

## 소스 근거

- [Engine/RenderEngine/Render/Temporal/TemporalReconstruction.h](../../Engine/RenderEngine/Render/Temporal/TemporalReconstruction.h)
- [Engine/RenderEngine/Render/Temporal/TemporalUpscalerHost.cpp](../../Engine/RenderEngine/Render/Temporal/TemporalUpscalerHost.cpp)
- [Engine/RenderEngine/Render/Scene/EnhancedTemporalViewState.h](../../Engine/RenderEngine/Render/Scene/EnhancedTemporalViewState.h)
- [Engine/RenderEngine/Render/Temporal/TemporalRuntimeControl.cpp](../../Engine/RenderEngine/Render/Temporal/TemporalRuntimeControl.cpp)
- [Engine/RenderEngine/Render/Scene/EnhancedSceneRenderer.cpp](../../Engine/RenderEngine/Render/Scene/EnhancedSceneRenderer.cpp)
- [Engine/RenderEngine/RHI/Temporal/TemporalSdk.props](../../Engine/RenderEngine/RHI/Temporal/TemporalSdk.props)
- [Engine/RenderEngine/RHI/Temporal/XeSSFrameGeneration.cpp](../../Engine/RenderEngine/RHI/Temporal/XeSSFrameGeneration.cpp)
