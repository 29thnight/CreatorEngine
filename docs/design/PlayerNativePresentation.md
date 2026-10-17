# Player native presentation과 Editor DX12 경계

2026-10-05 소스 변경. 빌드·컴파일·GPU 실행·테스트·벤치마크 결과가 아니다.
기준은 master `5ea5221ce647b84315382674183ea30c7bb470b2`다. PR #118의 소유권 변경과
작업 중 추가된 DX12 device-removal 주입 및 package layer 설정 복사를 그대로 보존한다.

## 1. 출력 소유권

Player는 `PlayerPresentation`의 게임 전용 구현 하나를 소유한다. 구현은 기존
`DX12DeviceResources` 또는 `VulkanDeviceResources`를 사용해 acquire, 최종 합성,
resize, 제출, native swapchain Present를 맡는다. 새로운 RHI/device 프레임워크를
만들지 않는다. 공용 렌더러는 기존 `IDisplayPresentationSink`로 완료 이미지만 게시한다.

- DX12: 완료된 공유 RGBA8 이미지에 consumer lease를 취득하고 fullscreen triangle로 표시
- Vulkan: 기존 scene readback → 최신 CPU 이미지 한 개 → Vulkan upload 전달을 유지하고,
  마지막 합성과 swapchain Present는 native Vulkan으로 실행
- 두 경로 모두 ImGui context, ImGui backend, Editor panel 또는 HostImGuiPresentation을
  요구하지 않는다. Shipping도 같은 native 출력 경로다
- Vulkan 전달을 zero-copy라고 부르지 않는다. readback/upload의 비용·지연은 남아 있다
- Player의 기존 Runtime Canvas/UI는 scene 결과에 포함되며 기존 입력 경로를 유지한다

`PlayerPresentation.slang`은 기존 shader compiler/cache와 패키지 shader closure를 사용한다.
이미 display-encoded인 RGBA8 UNORM 결과를 UNORM surface에 선형 샘플링하므로 sRGB를
중복 적용하지 않는다. 크기가 다르면 기존 fullscreen 이미지처럼 창 전체로 늘린다.
Vulkan은 지원되는 RGBA8/BGRA8 UNORM surface가 없으면 명확하게 실패한다.

## 2. 완료와 수명

DX12 producer의 shared allocation은 simultaneous-access texture가 아니며, producer는
COPY_DEST 상태로 게시한다. consumer는 COPY_DEST → PIXEL_SHADER_RESOURCE → COPY_DEST로
기록하고, producer 완료 fence와 기존 consumer lease가 두 device의 접근 순서를 보호한다.
HANDLE 숫자만으로 캐시 신원을 판단하지 않고 allocation의 lease 신원을 함께 확인한다.

- consumer lease, 열린 resource와 descriptor는 consumer GPU 완료까지 보유한다
- EndFrame 성공은 제출 요청 수락이고, Present 반환은 GPU 소비 완료나 scan-out 완료가 아니다
- 제출 실패 이후의 불명확한 사용은 정상 완료로 취급하지 않는다
- Player 종료는 PT/RT를 멈추고 consumer native 사용을 정리한 뒤 producer를 해체한다
- 부팅 중 예외에서도 설치한 sink/callback과 joinable thread를 회수한다
- native 완료를 증명할 수 없는 비정상 종료는 안전하지 않은 자원 파괴 대신 소유자를
  보존하고 실패로 보고한다. 정상 종료의 누수나 재시도 캐시로 사용하지 않는다

Vulkan CPU mailbox의 한 mutex는 RT→PT 데이터 전달에만 사용한다. 제출 큐를 감싸는
새 mutex는 없다. 업로드한 픽셀과 source frame/camera/scene/resize 신원은 같은 값 묶음이다.
producer의 새 게시가 이미 시작된 표시 프레임의 신원을 바꾸지 않는다.

최소화/0 extent는 acquire를 쉬고 복원·동일 크기 OUT_OF_DATE는 native view와 swapchain을
함께 재생성한다. 숨긴 64×64 smoke는 여전히 HWND와 swapchain을 갖는 offscreen 실행이며
headless가 아니다. smoke 성공에는 실제 게임 이미지의 native 합성 제출과 실패하지 않은
Present 호출도 필요하다. DXGI_STATUS_OCCLUDED는 숨긴 창에서 허용하지만, 새 compositor 출력이나
scan-out 완료로 세지 않는다. 종료 시 consumer 완료 확인이 실패하면 최종 exit code도 실패다.

## 3. Editor는 빌드 고정 DX12

`ImGuiHost`가 보유하는 타입은 `std::unique_ptr<ImGuiDx12Shell>`이다.
Vulkan ImGui shell, renderer backend 선택 enum과 공통 renderer 인터페이스를 제거했다.
Host 프로젝트에 Vulkan ImGui translation unit이나 Vulkan include root가 없다.
Scene, Game, material preview의 live renderer도 처음부터 constexpr DX12로 생성한다.

Editor launch code는 변경할 수 없는 `FixedDX12` host 정책을 넘긴다. 공용 RuntimeSettings는
그 경로에서 `render.backend`, `renderBackendDx12`, `imguiBackendDx12`를 조회·검증하지 않는다.
Vulkan 설정을 발견한 뒤 거절하거나 normalize하는 방식이 아니다. Editor 상태/로그는
build-fixed DX12를 보고하며 settings 저장도 최상위 runtime backend를 덮어쓰지 않는다.

공용 RuntimeSettings 모듈에는 Player용 설정 parser가 남아 있다. Player는 패키징이 투영한
`render.backend`로 DX12/Vulkan을 선택한다. Editor의 고정을 이유로 공용 Vulkan RHI를
컴파일에서 제거하지 않으며, Player의 `build.render.backend` 선택은 독립적이다.

Editor 전용 regression harness는 backend 키를 바꾸지 않는다. Vulkan Editor 실행 요청은
지원하지 않는다고 명시하며 DX12 실행을 Vulkan PASS로 보고하지 않는다. 별도 native Vulkan
probe와 Player 검증 경로는 유지한다.

## 4. 컴파일·빌드 경계와 증거의 범위

공용 bootstrap과 role-neutral CommandCore를 `Engine/RuntimeHost`로 이동했다.
Player는 Editor 경로의 소스를 함께 컴파일하거나 Editor include root를 열 필요가 없다.
Editor 생성 reflection은 Editor 프로젝트들에만 주입하고 공용 runtime에는 주입하지 않는다.

- Player의 HostImGuiPresentation ProjectReference 제거
- Player와 그 Engine native dependency closure에 MSBuild dependency gate 적용
- runtime의 Editor project/source/include/generated-reflection 의존 및 Engine 밖 프로젝트 참조 거부
- runtime include path 앞의 poison `imgui.h`/`imgui_internal.h`로 일반 ImGui include는 컴파일 오류
- Player에서 Editor Host public header를 직접 열면 CE_PLAYER guard의 컴파일 오류
- concrete Host renderer에 다른 backend를 대입하는 것은 타입 불일치

이 셋은 서로 다른 증거다. 헤더의 #error/구체 타입은 컴파일 계약, MSBuild gate는 평가된
빌드 의존 계약, 최종 binary의 symbol/import 목록은 링크 결과다. vcpkg는 공용 include/lib
설치를 사용하므로 ProjectReference 제거만으로 ImGui header가 보이지 않는다고 주장하지
않는다. 모든 가능한 절대 경로/매크로 우회를 막는 보안 sandbox도 아니다.

`python Tools/regression/verify-player-presentation-boundary.py`는 source/XML만 검사한다.
Debug/Release와 Shipping 조건을 보수적으로 합쳐 7개 runtime 프로젝트 그래프, Engine/Player
source, Host concrete 타입과 Editor 고정 정책을 확인한다. 이 검사 통과는 compiler 실행이나
최종 ImGui symbol 0의 증명이 아니다. vcpkg 자동 linker input도 별도 최종 확인 대상이다.

## 5. Development와 Shipping

Development/Shipping은 기존 `EngineShipping` 축이며 Debug/Release 최적화와 독립적이다.
Release Development가 가능하고, 별도의 중복 빌드 모드를 만들지 않는다. Editor의
Development Build 선택은 `build.development`로 저장하고 일치하는 prebuilt distribution으로
패키징한다. 설정과 선택한 배포본이 다르면 다른 엔진으로 몰래 바꾸거나 재빌드하지 않는다.

Development는 기존 profiler와 Player command registry를 사용한다. 네이티브 Player의
source debug information과 사용 가능한 PDB를 보존하지만 새 원격 debugger, managed debugger
attach protocol이나 Unity와의 기능 동등성을 구현한 것은 아니다. 기존 SceneRuntime Release의
LTCG/debug-information 예외는 유지한다.

HTTP command service는 여전히 명시적인 `--command-service` 요청에만 열린다. 기본값,
loopback/auth 정책과 외부 접근 권한을 바꾸지 않는다. Shipping은 개발자 service와 command
registration을 컴파일에서 제외한다. 로컬 CLI/commandlet과 실제 지원 명령은 기존 Player의
축소 registry를 따르며 Editor 저작 명령을 들이지 않는다.

## 6. Phase 4.5 이후 경계

게임 전용 native presenter가 swapchain/present 소유권 교체 지점이다. 향후 FG provider는
acquire, pacing, resize, generated present를 소유할 수 있지만 실제 source frame과 생성 출력의
신원은 구분해야 한다. 실프레임 history/시뮬레이션/측정이 생성 프레임 수로 진행되면 안 된다.

SDK가 입력을 최종 소비할 때까지 color/depth/motion/exposure/UI 소유권을 연장해야 하며,
현재 Present 반환을 provider 완료로 대신 쓰면 안 된다. 이번 변경에는 SDK 의존, frame
interpolation, Editor FG, capability fallback 구현이 없다. RG5/6와 별도 cache 작업도 바꾸지 않는다.

## 7. 소유자 수용 목록 — 이번 작업에서 실행하지 않음

1. Windows Debug/Release × Development/Shipping의 Player와 DX12 Editor 빌드
2. 양 Player backend의 cook/package, PDB/manifest, fullscreen/resize/minimize/restore/종료 확인
3. 입력·Canvas UI·최종 색/감마/방향/스케일과 숨긴 smoke의 native 합성 제출 및 occluded 보고 확인
4. 부팅 실패, device loss, 제출/Signal/Present 실패, 지연 consumer와 동일 크기 OUT_OF_DATE 주입
5. source lease 조기 반환·반복 재사용·종료 hang과 unproven-completion quarantine 확인
6. Development CLI/commandlet 성공·실패 exit code와 opt-in HTTP, Shipping 거절/심볼 격리 확인
7. Player 및 runtime closure의 link map/import/symbol 목록에서 Editor/ImGui 참조 부재 확인
8. backend 설정이 Vulkan이거나 잘못된 값이어도 Editor가 그 키를 해석하지 않고 DX12로 시작하며,
   Player Vulkan 패키징 선택은 그대로 적용되는지 확인

정적 검사만으로 실행 성공, 이미지 동등성, 성능 개선 또는 검증 레이어 0을 주장하지 않는다.
