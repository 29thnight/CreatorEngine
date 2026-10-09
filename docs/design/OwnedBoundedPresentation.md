# 소유권과 대기량을 명시한 표시 경로

기존 scene packet·display token·host frame·DeviceResources를 확장한다. 별도 렌더러,
스케줄러, FG provider 프레임워크는 만들지 않는다. 이번 변경은 **코드 수정과 정적 검토만**
수행했다. 컴파일·엔진 실행·GPU/런타임 검증은 저장소 소유자가 수행한다.

## 1. 소유권과 완료 신호

- GT가 실프레임 입력을 불변 packet으로 밀봉한다. 대기 packet은 최신 하나만 보존하되
  proxy lifecycle delta는 순서대로 합친다. 기존의 안전한 update 압축만 superseded 값을
  버린다. RT가 이미 소비 중인 packet은 바꾸지 않는다. 필수 명령 payload가 넘치면
  누락시키는 대신 기존 producer 역압력을 유지한다.
- 표시 이미지는 생산자 GPU fence 완료 뒤 게시한다. DX12 공유 이미지를 열 때 기존
  display lifetime mutex 아래 consumer lease도 취득한다. Host는 CPU 기록부터
  **자기 GPU sampling 완료까지** 이를 보유한다.
- Host fence 완료가 lease를 해제한다. 생산자의 슬롯 선택도 같은 mutex 아래에서
  게시되지 않았고 consumer가 없는 token만 고른다. ComPtr가 살아 있다는 사실만으로는
  그 allocation의 픽셀 덮어쓰기가 안전해지지 않는다.
- CPU 제출 ticket, 생산자 GPU 완료, 소비자 GPU 완료, native Present 반환, 물리적
  scan-out은 서로 다른 사건이다. 장치 제거 또는 완료를 증명하지 못한 경로는 슬롯을
  재사용 가능으로 바꾸지 않는다.
- Vulkan의 기존 readback → CPU 복사 → host upload는 그대로다. 생산자 공유 이미지
  lease가 필요 없는 복사 경로이며, zero-copy로 바뀐 것이 아니다.

## 2. 진입 제한과 프레임 나이

씬과 host의 상한은 **각 큐의 상한**이다. 씬 view 제출 두 개가 GT packet 하나에 속할
수도 있다. `N−2` 프레임을 고르거나 입력부터 화면까지 두 프레임이라고 보장하지 않는다.

- RT는 GPU 여유를 기다린 **뒤** 최신 대기 packet을 선택한다. 요청한 유효 view 수와 2 중
  작은 만큼의 여유를 먼저 확보해, 흔한 Editor+Game 쌍이 이전 제출 하나 때문에 갈라지는
  경우를 줄인다. 보수적인 진입 정책의 처리량 비용은 실측이 필요하다.
- 대기는 전용 RT의 condition variable이 담당한다. GPU 작업이 남은 동안 2ms 간격으로
  완료를 확인하며 작업 풀을 점유하거나 busy-spin하지 않는다. 완전히 유휴일 때는 기존의
  100ms 단위 profiler idle scope를 유지한다.
- GT가 멎어도 이미 제출된 GPU 결과는 수집한다. 실제 이미지 완료 알림이 더 큰 GT
  frame ID 없이 PT를 깨운다. 새로 안전한 이미지가 없으면 기존 완료 이미지를 재사용한다.
- 같은 프로세스의 steady clock으로 원본 packet 캡처 이후 시간을 잰다. 50ms soft budget을
  넘고 더 최신 입력이 기다리면 불필요한 픽셀 작업을 생략한다. 250ms 진행 예외와 마지막
  유휴 입력 허용으로 무한히 생략하기만 하는 상태를 피한다.
- 이미 실행 중인 GPU 작업, 오래 걸리는 셰이더 준비, 느린 장치, compositor와 scan-out은
  이 정책으로 취소하거나 시간을 보장할 수 없다. 50/250ms는 hard latency 보장이 아니다.
  같은 이미지를 다시 보여도 원본 시간을 다시 찍지 않는다. 캡처 나이는 입력-광자 지연이 아니다.

Host는 미완료 GPU 프레임 하나까지만 진입시킨다. DX12는 waitable swapchain과 maximum
frame latency 1을 함께 사용하며, 진입 대기 한 번은 100ms 뒤 실패 반환해 resize/stop 처리를
막지 않는다. Vulkan은 FIFO image 수 2를 요청하되 surface의 최소 수를 존중한다. GPU 완료는
compositor의 이미지 소비 완료를 뜻하지 않으므로 Vulkan의 OS 표시 큐 길이는 별도로 남는다.

Native Present는 자신의 직렬화된 표시 소유자에서 동기 호출한다. 그 전에 자기 제출의 CPU
ticket을 기다리므로 공용 RHI FIFO에서 native Present가 scene 제출을 막지 않는다.
Fire-and-forget Present가 아니며 native API 자체의 지연을 없앴다는 뜻도 아니다.

## 3. 잠금 감사

새 mutex나 spin lock을 추가하지 않는다. Host 기록·Present·resize는 기존 PT 소유 순서로
직렬화하고, 해체는 PT join 뒤 실행한다. Vulkan도 같은 owner의 제출 ticket 완료가 native
queue submit과 present의 동시 접근을 막는다. 향후 새 host 제출자나 SDK queue를 붙이면 이
소유권 계약을 다시 검토해야 한다. shared_ptr가 lock-free라고 주장하지 않는다.

- scene-structure mutex: backend frame 획득·GPU 여유 대기 동안 해제하고, UI 네이티브
  기록이 끝난 뒤 다시 해제한다. 라이브 패널, 사용자 draw callback, GT packet 캡처에는
  기존 보호가 여전히 필요하다
- display-lifetime mutex: consumer 취득과 producer 재사용 판단을 같은 경계로 만든다.
  GPU 대기·기록·제출 동안 붙들지 않는다
- render-queue mutex/CV: 순서 있는 delta 전달, 최신 packet 교체, payload 역압력에 필요하다
- CPU-image mailbox mutex: Vulkan RT → PT 복사 데이터 전달에 계속 필요하다

전체 에디터 패널의 immutable snapshot 전환과 GT 명령 전용 편집은 별도 작업이다.
현재 라이브 패널을 불변이라고 부르거나 편집 의미를 바꾸지 않는다. 현재 꺼진 detached
ImGui viewport가 켜지면 추가 draw 기록까지 보수적으로 잠금을 유지하고, 별도 GPU queue의
완료를 추적하지 않는 공유 이미지 import는 실패 처리한다.

뷰 세 개 또는 consumer lease 때문에 막힌 개별 슬롯까지 한 packet에서 반드시 제출한다고
보장하지 않는다. 그런 view는 후속 GT 입력이 필요할 수 있다. 기존 DX12 resize의 과거
retired display allocation은 interop shutdown까지 보유하므로 큐 상한은 메모리 상한이 아니다.

## 4. Phase 4.5 경계

정본은 [TemporalReconstructionPlan.md](../plans/TemporalReconstructionPlan.md)의
§2.3·§3.1·§3.3·§5.4·§7·§8·§9다.

- TR은 motion·jitter·history·해상도 계약, TU는 graph 연산, FG는 shell/RHI의 present
  소유권과 게임 루프 지연 marker를 맡는다
- Editor viewport의 `live_present`는 공유 texture 게시다. Swapchain이 아니므로 FG
  활성화 지점이 아니다. 이번 변경에 editor FG, 가짜 보간 pass, vendor SDK 의존성은 없다
- 향후 provider가 native swapchain을 교체하고 acquire·pacing·resize·present를 소유할
  수 있다. 이 확장점은 기존 backend 안에 남기며 별도 공용 provider 계층을 미리 만들지 않는다
- 실제 source packet/camera/input ID와 host 표시 순서, provider의 generated output 순서는
  구분한다. 생성 프레임으로 GT simulation, 실프레임 history, golden capture, 실프레임 성능
  지표를 진행시키면 안 된다
- Provider가 쓰는 실프레임 texture와 depth/motion/exposure/HUD/UI는 일반 Present 반환
  이후에도 살아야 할 수 있다. 기존 자원 소유권을 provider의 **최종 소비 완료**까지 늘려야 한다.
  정확한 입력 목록과 동기화 요구는 provider와 버전에 따라 다르다
- Provider prepare/configure/present와 resize/history reset/shutdown은 provider의
  직렬화·완료 계약을 따른다. SDK가 pacing/queue를 소유해도 앱의 입력 재사용 의무가 사라지지
  않는다. Phase 4.3의 RenderGraph lifetime이나 Q0 계약을 여기서 재정의하지 않는다
- 런타임 capability, TU/FG 독립 선택, 명시적 미지원 사유와 fallback은 TU0/FG0의 일이다.
  이번 변경은 SDK 지원을 주장하거나 vendor ID·과거 GPU 세대 표를 조건문으로 굳히지 않는다

FG0 착수 시 다시 확인할 공식 입력:
[NVIDIA Streamline DLSS-G](https://github.com/NVIDIA-RTX/Streamline/blob/main/docs/ProgrammingGuideDLSS_G.md),
[AMD frame interpolation swapchain](https://gpuopen.com/manuals/fidelityfx_sdk/techniques/frame-interpolation-swap-chain/),
[Intel XeSS-FG](https://github.com/intel/xess/blob/main/doc/xess_fg_developer_guide_english.md).

## 5. 소유자 검증 목록 — 이번 변경에서 실행하지 않음

`dx12.live status`의 `admission`에 진입·생략·역압력 통계가 있고, 표시 target별로
캡처 나이와 input/camera 신원을 낸다. `render.live.wait`는 CPU packet 소비임을 명시하며
GPU·화면 완료를 기다리는 명령이 아니다.

1. Editor/Player DX12 Debug·Release를 빌드하고 host interface 구현을 확인한다.
   유지되는 Vulkan 경로의 컴파일도 확인하되, Phase 4.5 Vulkan 실행 동등성은 Phase 4.9 소유다
2. GPU validation을 켜고 반복 sampling, 느린 host GPU, view 교체, resize·최소화·복원,
   scene 파괴·종료를 확인한다. consumer가 읽는 이미지를 producer가 덮어쓰면 실패다
3. GPU 포화·긴 CPU 기록·coalescing·GT 발행 중단에서 진입 나이와 취득 이미지 나이를
   따로 확인한다. lifecycle delta 순서와 마지막 **제출된** 이미지의 게시·표시를 확인한다
4. 제출/Present 실패·장치 제거를 주입해 거짓 fence 완료, 조기 lease 해제, shutdown hang,
   불명확한 자원 재사용을 확인한다. Vulkan 동일 크기 OUT_OF_DATE는 shell 경유로 view와
   swapchain을 함께 재생성하고, surface/device loss는 명시적 실패로 남겨야 한다
5. 실프레임·camera·input 신원, profiler v3와 픽셀을 비교한다. Host 대기가 GT에 전파되지
   않는지 측정한다. 지연·처리량 개선 여부는 소스만으로 판정하지 않는다
