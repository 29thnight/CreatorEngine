# 렌더링 파이프라인 구조 개선 구현 기록

기준: `master`의 `ea87639ece7b6068456940ddddd83a9d23f86c58`.
작업 PR: https://github.com/29thnight/CreatorEngine/pull/190

이 문서는 실제 구현과 검증 상태를 구분한다. 아래 여섯 범위의 코드와 실패 경로 처리를
통합했다. portable 회귀는 실제 production C++의 CPU 동작을 검사한다. Windows 빌드와
실제 GPU 검증은 아직 실행하지 않았으며, 이 검증이 끝날 때까지 PR을 Draft로 유지한다.

## 범위별 구현

| 범위 | 구현 | 현재 검증 및 남은 작업 |
| --- | --- | --- |
| 불변 GraphProgram / FrameBindings | 논리 선언·스케줄을 불변 소유자로 공유한다. 직접 인덱스 바인딩과 기존 Declare 본문을 이용하는 스트리밍 재바인딩을 제공한다. live DX12/Vulkan 슬롯과 뷰가 프로그램을 재사용한다. 구조가 바뀌면 현재 프레임에서 전체 compile로 돌아간다. | 실제 그래프 TU의 프로그램·epoch·소유권·범위·큐·번역 실패 테스트 및 ASan/UBSan 통과. |
| 뷰별 입력과 실행 상태 | Scene 수집 상태와 뷰별 준비 입력을 분리한다. 카메라·광원·그림자·decal·sprite·UI·gizmo 및 자산 pin을 const 소유자로 게시한다. SceneHost와 공통 패스 상태를 세 뷰에 각각 둔다. 준비 풀은 뷰당 4개이며 살아 있는 입력은 덮어쓰지 않는다. | 실제 소유권 타입을 사용하는 풀 회귀 및 ASan/UBSan 통과. native pass 초기화/시각 결과 검증 필요. |
| 타입이 있는 기능 합성 | 35개 builtin 노드의 65개 포트 목록을 typed contract로 선언한다. kind·semantic·format·extent·required/optional 계약과 현재 그래프 epoch를 검사한다. 복수 contributor를 AfterOpaque / BeforePostProcess / BeforePresent에 합성한다. | 실제 feature/graph 코드 81개 검사와 ASan/UBSan 통과. Editor GPU overlay 회귀 필요. |
| 뷰별 승인·갱신 정책 | Game 연속·Scene 기본 60 Hz·Material Preview 입력 revision 기반 갱신. Host override, 진단 강제 갱신, dirty 재시도, 기본 250 ms 기아 방지. 총 GPU credit은 2이며 한 credit부터 작업을 승인한다. | 독립 정책 회귀 통과. 실제 host 부하와 입력 지연 측정 필요. |
| subresource / 다중 큐 / aliasing | mip·layer·aspect·buffer 범위별 hazard 및 barrier를 처리한다. 불변 imported texture는 지원되는 native 공용 읽기 상태에 한해 cross-queue 공유한다. alias는 모든 이전 사용이 모든 다음 사용보다 DAG상 앞선다는 증명으로만 허용한다. | 실제 SubmitQueues를 거치는 CPU fixture와 sanitizer 통과. 실제 DX12/Vulkan validation 및 heap 사용량 측정 필요. |
| CPU executor / GPU queue / GPU 표시 | CPU native/software와 GPU primary/owned/graphics+compute 정책을 분리한다. 번역 coordinator가 RHI 제출 FIFO를 점유하지 않으며 같은 owner의 순서를 보존한다. owned 큐도 제출 전에 모든 batch의 번역을 끝낸다. 큐별 완료 집합을 기록한다. DX12/Vulkan이 같은 GPU 표시 패킷·소비 lease 계약을 사용한다. | 최종 RHI 33,895 checks / 1,756 scenarios, owned software queue 5,474 checks, Vulkan/Win32 경계 모의 검사 524 checks 모두 일반·ASan/UBSan 통과. 실제 native 검증은 미실행. |

## 구현의 경계

### 프로그램과 프레임 수명

`GraphProgram`은 native 자원이나 프레임별 콜백을 소유하지 않는다. `FrameBindings`와
스트리밍 Declare 경로는 매 프레임 현재 imports·callback·owner를 결합한다. exact hit는
불변 usage 저장소와 구조 스케줄을 재사용한다. 동적인 recording 비용은 구조 동일성에서
분리하지만 CPU recording plan과 timing identity에는 반영한다. native 초기 상태, barrier,
물리 allocation은 현재 프레임에서 다시 검증한다.

live 경로는 기존 패스 Declare를 계속 호출한다. 전체 선언 비용이 사라졌다고 주장하지 않는다.
프레임별 arena 복사와 중복 구조 구축을 줄이는 경로이며, 직접 `BindProgram` API는 고정
논리 topology를 가진 호출자가 현재 frame binding을 인덱스로 제공할 수 있게 한다.

완료되지 않은 그래프나 다른 소유자가 남은 그래프는 Reset하지 않는다. 준비 입력은 그래프의
선언 arena에 소유자를 남긴다. Vulkan 표시 이미지는 그래프와 별도로 소유하므로, GPU 완료와
batch 소유권 회수가 끝난 그래프는 입력 풀 반환을 위해 Reset할 수 있다.

### 기능 확장 계약

HDR 기여 지점은 builtin `LitColor`를, 표시 직전은 `DisplayLdr`를 갱신할 수 있다.
GBuffer·temporal export·scene command 등의 builtin 포트는 읽기 계약만 제공한다.
plugin-private 슬롯은 일반 의존성 검증으로 연결한다. 허용되지 않는 builtin 교체는
contributor·stage·node·slot을 포함한 오류로 거부하며, callback이 선언하지 않은 슬롯에
숨겨서 Set하는 경우도 검출한다. disabled passthrough와 per-view 활성 조건도 검증 대상이다.

등록 목록은 다음 pipeline assembly에서 snapshot으로 고정하고 그 소유자가 callback의
수명을 유지한다. 기존 단일 contributor setter는 호환성을 유지하며 실패 시 기존 목록을
보존하는 방식으로 교체한다.

### 갱신·큐·메모리 계약

픽셀 작업의 갱신 제한은 Scene 수명 delta 소비, 완료 회수, 종료 drain을 막지 않는다.
성공한 제출만 주기를 소비하며 material preview의 임시 fallback은 완성된 preview로
캐시하지 않는다. 뷰별 temporal 시간은 마지막 성공 제출에서 경과한 시간을 사용한다.

CPU 기록 executor와 GPU 큐 배치는 독립이다. Vulkan은 현재 실제 queue가 하나이므로
owned queue 요청을 primary graphics로 해석하고 그 이유를 진단에 기록한다. 지원되는
DX12 조합은 기존 queue 경계로 실행한다. translation 중에는 다른 owner의 실행 가능한
제출이 진행하지만 같은 owner의 tail·lifecycle 순서를 앞지르지 않는다.

owned software queue는 모든 prologue·main·epilogue batch의 `PrepareSubmissionBatch()`를
완료한 다음 native 제출 transaction으로 들어간다. 뒤쪽 batch 번역이 실패하면 앞쪽에
준비된 native batch가 있어도 GPU wait·submit·signal을 실행하지 않는다. 취소는 준비된
native 저장소와 수명 owner를 회수하며 software arena credit을 반환한다. 실제 제출이
일부 진행된 뒤의 실패는 이미 accepted된 큐별 완료점을 보존한다. 유한한 완료점을 확보할
수 없는 슬롯은 재사용하지 않고 안전한 pipeline teardown 경로로 보낸다.

메모리 alias는 선형 pass 번호가 겹치지 않는다는 이유만으로 허용하지 않는다. 기존 DAG의
happens-before 증명을 요구하므로 독립 작업에 억지 직렬화 간선을 만들지 않는다. 이 규칙은
기존 단일 큐 interval 방식보다 heap을 더 사용할 수 있다. queue cost 상수는 분석용
추정치이며 실제 측정된 성능 개선으로 표시하지 않는다.

### 표시 이미지 수명

GPU 표시 패킷은 실제 생산자 완료점·유한한 완료 관측 값·adapter identity·픽셀 metadata·
transport owner·소비 lease를 함께 전달한다. DX12와 Vulkan의 기본 표시 출력은 외부
소비자가 읽을 수 있는 공통 상태로 돌려놓는다. temporal export는 별도 기존 계약을 유지한다.

Vulkan은 같은 어댑터에서 지원되는 D3D12 공유 이미지를 import하고 최종 LDR를 GPU에서
복사한다. Vulkan의 외부 acquire는 단일 mip/layer color image의 전체 CopyDest Write에만
허용한다. 이전 내용을 읽는 Read/Modify나 부분 쓰기는 거부하고, generic Common 전이를
끼우지 않은 ExternalShared → CopyDest → ExternalShared 순서를 유지한다.
GPU 경로는 최대 표시 뷰 3개 + pending 2개 + 소비 staging 1개인 6개 슬롯을
사용한다. producer 완료, 게시 소유권 해제, host의 마지막 GPU 사용을 모두 확인한 슬롯만
재사용한다. 해당 import가 지원되지 않으면 이유를 기록하고 3개 CPU readback 슬롯으로
돌아간다. 이미 제출한 뒤 EndFrame이 실패해도 실제 완료 전에는 슬롯을 재사용하지 않고,
실패한 프레임은 게시하지 않는다. 높은 fence 값만으로 앞선 번역 성공을 추정하지 않고,
원래 scene batch와 EndFrame tail의 CPU 제출 ticket 성공도 확인한다. 표시 sink의 GPU
공유 능력이나 adapter가 바뀌면 안전하게 pipeline을 재구축한다. Editor/Player는 표시
capability와 adapter identity를 atomic 값으로 게시하므로, 렌더 스레드가 표시 스레드의
native device나 일반 active 상태를 직접 읽지 않는다.

회귀는 실제 제출 FIFO에서 앞선 번역을 실패시킨 뒤, 이미 admission된 성공 tail이 더 높은
가상 GPU fence를 신호하도록 만든다. 이 경우에도 `PollRHIDisplaySubmission()`이 실패를
반환하고 화면 게시가 거부됨을 확인한다. GPU 완료와 CPU 제출 성공은 독립적인 증거다.

## 진단

graph snapshot JSON은 일반 usage/barrier뿐 아니라 반복 phase의 usage·first barrier·repeat
barrier에도 texture mip/layer/aspect 또는 buffer byte 범위를 기록한다. 범위 끝까지를
나타내는 정수 최댓값 sentinel은 JSON에서 `-1`이며 `subresourceRangeRemaining`에 명시한다.
native owner는 진단 snapshot으로 복사하지 않아 진단 조회가 GPU 자원 회수를 연장하지 않는다.

## 검증 기록과 재현

검증 환경은 Linux / GCC 13이다. portable fixture가 사용하는 production TU·소유권·스케줄러와
대체하는 native 경계는 각 runner의 `--help` 및 결과 JSON에 명시되어 있다. 이것은 Windows
SDK ABI, 실제 GPU barrier, 픽셀 동등성 또는 프레임 성능을 증명하지 않는다.

```sh
g++-13 -std=c++20 -Wall -Wextra -Werror Tools/regression/LiveViewSchedulingTests.cpp -o /tmp/creator-live-view-scheduling
/tmp/creator-live-view-scheduling

g++-13 -std=c++23 -Wall -Wextra -Werror -fsanitize=address,undefined -fno-omit-frame-pointer Tools/regression/PreparedViewInputPoolTests.cpp -o /tmp/creator-prepared-view-inputs
ASAN_OPTIONS=detect_leaks=0 /tmp/creator-prepared-view-inputs

python3 Tools/regression/verify-render-graph-program.py
ASAN_OPTIONS=detect_leaks=0 python3 Tools/regression/verify-render-graph-program.py --sanitize
python3 Tools/regression/verify-live-pipeline-contracts.py
ASAN_OPTIONS=detect_leaks=0 python3 Tools/regression/verify-live-pipeline-contracts.py --sanitize

# 아래 두 경로를 실제 checkout 위치로 바꾼다.
CE_RENDER_ENKITS=/path/to/enkiTS
CE_RENDER_VULKAN_HEADERS=/path/to/Vulkan-Headers

python3 Tools/regression/verify-rhi-software-queue.py --enkits "$CE_RENDER_ENKITS"
python3 Tools/regression/verify-rhi-software-queue.py --enkits "$CE_RENDER_ENKITS" --sanitize

python3 Tools/regression/verify-rhi-software-portable.py --enkits "$CE_RENDER_ENKITS" --output /tmp/creator-rhi-normal.json
python3 Tools/regression/verify-rhi-software-portable.py --enkits "$CE_RENDER_ENKITS" --sanitize --output /tmp/creator-rhi-sanitized.json

python3 Tools/regression/verify-vulkan-software-commands.py --enkits "$CE_RENDER_ENKITS" --vulkan-headers "$CE_RENDER_VULKAN_HEADERS" --output /tmp/creator-vulkan-normal.json
python3 Tools/regression/verify-vulkan-software-commands.py --enkits "$CE_RENDER_ENKITS" --vulkan-headers "$CE_RENDER_VULKAN_HEADERS" --sanitize --output /tmp/creator-vulkan-sanitized.json
```

enkiTS는 `0289cf6ffce86697e1f46cb4eeca1c946bffec71` (v1.12), Vulkan-Headers는 공식
v1.4.328 헤더를 사용했다. dependency를 변경하거나 테스트용으로 production 코드를 바꾸지 않는다.

- 그래프와 typed feature: 실제 production TU를 compile/link하며 program 재사용, 두 프레임
  streaming, native schema/epoch 거부, 부분 범위 hazard, alias happens-before, 부분 제출
  retirement, 외부 이미지 acquire/release, 제출 전 번역 실패를 실행한다.
- primary RHI: 실제 recorder·translator·FIFO·arena·mimalloc과 1/2/4/8-worker scheduler를 사용한다.
  최종 일반·sanitizer 실행은 각각 **33,895 checks / 1,756 scenarios**다.
- owned software queue: 실제 8-worker scheduler에서 **5,474 checks**, 일반·sanitizer 모두 통과했다.
  이미 준비된 앞쪽 batch가 있어도 뒤쪽 준비 실패에서는 native 제출이 0회다.
- Vulkan/native 경계: **524 checks**, 1/4/8개 실제 번역 타깃과 공유 이미지 생성/import의
  **11개 주입 실패 rollback**을 포함한다. DX12 subresource helper도 실행하지만 D3D/DXGI/COM
  API 경계는 모형이므로 Windows SDK ABI 검사가 아니다.
- runner가 기록하는 입력 소스 hash는 실행 전후 일치했다.

- `git diff --check`: 통과.
- 새 C++/header의 MSBuild project 및 filter 등록: XML 구조와 item 중복 검사 통과.
- include boundary script: 기준 commit과 현재 모두 Linux에서 include 27개·physical debt 73개로 실패.
  같은 스크립트와 같은 source 범위로 기준을 복원해 비교했으며 새 간선·새 부채는 0개다.
  기존 allowlist를 수정하거나 통과한 검사로 표시하지 않는다.
- ASan/UBSan: 위 명시된 검증 통과. LeakSanitizer는 sandbox의 `/proc` task 접근 제한으로
  실행되지 않아 `detect_leaks=0`을 사용했다. leak 검증 통과라는 의미는 아니다.
- Windows Editor/Player 전체 빌드, 실제 DX12/Vulkan validation, multi-view 픽셀·resize·shutdown,
  실제 메모리·CPU/GPU 성능 비교: 미실행.

## 체크포인트와 완료 조건

첫 체크포인트에는 뷰별 승인 정책과 live 통합을, 두 번째 `b7471646c4987416d5175a19099d50987129cfdc`에는
여섯 범위의 코드와 portable 회귀를 푸시했다. 후속 커밋에는 owned software queue의 제출 전
번역, 실패한 batch 뒤 성공 tail의 표시 차단, 외부 이미지 상태 검증, capability 조회와
진단 범위 출력을 포함한다. PR 본문에 최종 커밋과 검증 상태를 함께 기록한다.

Windows SDK와 실제 GPU host가 없는 현재 환경에서 실행하지 못한 gate는 위에 명시했다.
여섯 범위의 구현 완료와 native 검증 완료를 별도로 표시하며, 미실행 gate를 통과한 것으로
간주하거나 성능 향상 수치를 만들지 않는다.
