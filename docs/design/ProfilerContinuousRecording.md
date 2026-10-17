# 연속 프로파일 기록과 비동기 보기

PR #117의 보완 계약이다. 빌드·엔진 실행·회귀 검사·벤치마크는 실행하지 않았으며,
아래 동작은 구현과 정적 검토 기준이다. 성능 개선 수치나 물리 화면 FPS를 주장하지 않는다.

## 녹화와 보기의 분리

- Record는 새 세대·새 라이브 링·새 임시 세션 파일을 만든다. 이전 세션의 마무리가
  끝나기 전에는 다음 Record를 받지 않는다. Starting은 수집기 적용 전 상태다.
- 600프레임과 128 MiB는 라이브 메모리의 기본 보존 한도다. 파일의 프레임 수나 녹화
  시간 한도가 아니다. 링에서 밀려난 프레임은 버리기 전에 파일 작성 큐로 넘긴다.
- GPU와 늦게 도착한 CPU/계수 표본이 같은 링 안의 원래 프레임을 채울 수 있도록,
  아직 링에 남아 있는 프레임은 Stop에서 마지막으로 넘긴다. 이미 밀려난 프레임보다
  늦은 표본은 다른 프레임에 섞지 않고 late loss로 센다.
- 기본 producer 페이지 풀은 8192페이지, 프레임 제출 큐는 600개로 제한한다.
  계수 제출 작업은 전체 작업 큐 65536개에서 admission을 제한한다. 설정의 0은
  무제한 대신 유한 기본 상한이다. 소유 프레임의 계수와 지연 표본에도 별도 상한이 있다.
- 파일 큐는 기본 64 MiB/64묶음이며 현재 쓰는 묶음도 예산에 포함한다. 라이브 수집은
  디스크나 큐 공간을 기다리지 않고 거절된 프레임·이벤트·계수를 센다. Stop의 최종
  꼬리는 수집기에서만 제한된 큐 공간을 기다린다. UI·GT·RT가 디스크를 기다리지 않는다.
- Stop은 한 종료 시각에서 producer 봉인을 요청하고 남은 시간 경계와 표본을 마무리한다.
  Frozen은 메모리 캡처 공개를 뜻하며 파일의 Finalized와 다르다. Save는 Finalized 이후
  현재 녹화 세션 전체를 내보낸다. Live Follow를 껐거나 다른 파일을 보고 있어도 동일하다.
- Clear는 recording/starting/pausing 중에 받지 않는다. 비동기 Clear의 반환 ticket은
  control_applied(ticket)으로 확인한다. UI는 정확한 완료를 확인한 뒤 다시 동기화한다.

## 파일과 복구

임시 파일은 시스템 임시 디렉터리 아래 고유 ceprof-* 디렉터리의 recording.ceprof다.
새 Record는 다른 경로를 사용하며 이전 spool은 자동으로 삭제하지 않는다. 디스크
오류에서는 경로와 오류를 표시한다. 최종 저장은 대상 옆 임시 파일에 제한된 버퍼로
복사하고 재검증한 뒤 교체한다. 원본 spool과 기존 대상은 복사 실패 때 보존한다.

CEPROF v1/v2를 계속 읽는다. 새 연속 파일은 v3이며 예전 엔진은 v3를 읽을 수 없다.
기존 encode_capture/save_capture는 제한된 불변 스냅샷의 v2 내보내기 API로 남는다.
Editor/Player의 Save 명령은 이 API 대신 세션 spool 복사를 사용한다.

모든 수치는 little-endian이고 구조체 padding을 그대로 쓰지 않는다.

| 부분 | 순서 |
| --- | --- |
| 파일 머리, 16바이트 | magic `CEPROF\0\0` 8바이트, version u32=3, reserved u32=0 |
| 레코드 머리, 32바이트 | magic u32=`0x4B435043`, type u32, sequence u64, payload bytes u64, payload CRC32 u32, 앞 28바이트 CRC32 u32 |
| type=1 | 프레임 0개인 완전한 CEPROF v2 스냅샷. 시계·마커·스레드·계수 사전 |
| type=2 | engine frame u32, begin/end u64 각각, dropped events u64, event count u32, counter count u32, 이벤트/계수 필드 |
| type=3 | complete u8, unacked streams u32, 아래 12개 u64 필드 |

최종 레코드의 u64 순서는 written frames, submitted frames, first tick, last tick,
writer dropped frames/events/counters, source dropped counters, source dropped events,
source dropped frame boundaries, late CPU events, late GPU spans다. CPU 이벤트는 v2의
62바이트 필드 순서, 계수는 id u16/value f64/session u64/tick u64/task u64의 34바이트다.
마커·스레드·계수 사전은 이미 공개된 ID의 뜻을 바꿀 수 없고 뒤에만 추가할 수 있다.

레코드는 최대 32 MiB, 사전은 최대 16 MiB다. 초과 자료는 무제한 할당 대신 명시적
resource limit/손실로 처리한다. sequence, CRC, 개수/크기 곱셈, 범위, ID, enum,
유한 계수 값, 사전 일관성과 프레임 순서를 확인한다. CRC가 틀린 완전한 레코드는
복구 성공으로 숨기지 않는다. 불완전한 마지막 레코드/최종 레코드 부재는 검증된
접두 구간까지만 복구하고 recovered/incomplete로 표시한다.

작성 중 stream flush는 약 1초 주기로 하며 C++ stream buffer 배출이다. 물리 디스크의
전원 장애 내구성을 보장하지 않는다. 비정상 종료 시 아직 라이브 링과 작성 큐에
남아 있던 꼬리는 복구 파일에 없을 수 있다. 정상 Stop은 그 꼬리를 넘겨 마무리한다.

## 프레임 귀속과 완전성

CPU 이벤트는 종료 시각이 `(frame.begin, frame.end]`인 프레임에 속한다. 시작 시각은
프레임보다 앞설 수 있다. 수집기가 오래된 GT 경계를 처리할 때 먼저 도착한 미래
페이지는 그 종료 시각의 경계가 닫힐 때까지 남겨 둔다. 늦은 도착도 같은 경계 규칙을
쓴다. GPU 이벤트는 backend가 CPU 축으로 보정한 시각과 원래 제출 frame ID를 유지한다.
GPU 시각을 현재 GT 프레임의 CPU 종료 시각으로 억지 분류하지 않는다.

stream seal ack는 producer 꼬리 전달 여부다. 녹화 시간의 전체 보존, 디스크 저장 완료,
프레임 경계 손실 없음과 같은 뜻이 아니다. v3 complete는 최종 봉인과 모든 기록된 손실
정보를 함께 확인한다. UI에는 writer backlog와 writer/source/late 손실을 구분해 표시한다.

## 전체 개요와 상세 창

Open/View entire session은 background에서 파일을 검증한다. 전체 프레임의 이벤트를
메모리에 올리지 않고 최대 2048개 개요 bin과 sparse offset을 유지한다. 각 bin은
프레임 ordinal/engine frame 범위, 시간 범위, 최소·최대 프레임 길이, 이벤트/손실 수를 든다.
선택·드래그와 First/Previous/Next/Recent 600은 1~600프레임, 128 MiB 이하의 상세 창을
background에서 읽는다. 전체 개요는 요약이며 모든 이벤트를 한 번에 확대한 타임라인은 아니다.
긴 파일의 최초 검증 시간은 파일 크기에 비례하지만 메모리와 UI 작업량은 제한한다.

Live Follow는 보기만 제어한다. 파일 모드에서 늦게 도착한 live snapshot이 화면을
덮어쓰지 않는다. Clear/Open 세대가 바뀌면 오래된 작업 결과를 채택하지 않는다.

## 비동기 타임라인과 카메라 계측

reader 하나는 승인된 준비 작업 하나와 종류별 최신 요청만 가진다. window aggregate의
이벤트 복사·정렬, lane/depth interval index, 선택 표 집계와 파일 구간 읽기는 worker에서
수행한다. source capture/aggregate/index를 한 불변 결과로 공개하고 UI는 마지막 준비된
결과를 계속 그린다. 다른 캡처의 사전·시계·인덱스를 섞지 않는다. 스케줄러 거절/종료에
콜백이 실행되지 않아도 admission guard가 busy를 해제한다. UI에서 join이나 fallback
집계를 실행하지 않는다.

카메라는 PT 소유 GameInput 누적 판독과 PT 경과 시간을 사용한다. 기존 회전 감도
0.005 rad/raw count와 포커스/drag 재기준화를 유지한다. 이미지가 없다는 이유로
카메라 입력을 건너뛰지 않으며 picking/Gizmo는 완료 이미지의 유효성 조건을 유지한다.
23개 sparse render 계수와 EditorCameraPresentationSample instant의 cpu.tick은 같은
PT 입력 sequence다. input sequence/camera revision은 DX12 slot과 Vulkan CPU bridge의
기존 camera bundle에 함께 전달된다. 렌더 소비, GPU 완료, ImGui에 실제 참조한 이미지와
반복 참조를 구분한다. Present 호출 간격이나 ImGui 참조 횟수는 물리 scanout FPS가 아니다.

## 사용자 확인 항목

- Debug/Release, DX12/Vulkan, Editor/Player 빌드와 기존 진단 probe의 변경 계약 확인
- 1~2분 Record→Stop→Finalized→Save 후 첫 구간/중간/최근 구간과 전체 duration 확인
- Live Follow off, 이전 파일 보기, 반복 Record/Stop/Clear/Open/Save와 종료 중 마무리
- 디스크 가득 참/권한 오류/잘린 꼬리/CRC 손상/과도한 개수와 늦은 GPU·CPU 표본
- 넓은 live 창에서 빠른 snapshot 갱신, pan/zoom, 창 숨김/닫기와 scheduler 종료
- RMB/DPI/포커스 전환/resize/미준비 texture/카메라 직접 편집과 입력→이미지 신원 연결

## 기존 진단 도구의 소스 이행

standalone probe의 컴파일 목록에 새 파일 backend를 연결하고 Record 적용, Clear ticket,
파일 마무리를 명시적으로 기다리도록 호출부를 옮겼다. reader의 같은 세션 갱신 자극은
Pause→Record 대신 같은 녹화의 live snapshot을 쓴다. 측정 시간에 파일 마무리 대기를
섞지 않는다. 이 probe나 빌드 스크립트는 실행하지 않았다.

기존 mutation 73개 정의의 소스 anchor는 정적으로 하나씩 대응시켰다. 새 세션 계약으로
기존 자극이 더는 같은 결함을 입증하지 못하는 clear-keeps-generation, clear-without-seal,
scope-ignores-generation, pause-truncate-unmarked 네 항목은 사유를 보존한 명시적 unsupported
목록이다. 기본 실행은 이를 알리고 지원 항목만 수행하며, 해당 항목의 -Only 요청은
사유와 함께 실패한다. mutation kill이나 전체 회귀 통과를 확인했다는 뜻은 아니다.

legacy v1/v2 Open은 CRC·청크·프레임·계수 구조를 우선 검사한다. 이벤트와 계수의 세부
의미 검증은 선택 구간을 읽을 때 한다. v3에서는 전체 스캔 때 이벤트 소유권도 검사한다.

## 캡처 경계·종료 꼬리 보완 (2026-10-06)

이 절의 구현과 회귀 소스는 정적으로만 검토했다. 빌드·테스트·엔진 실행·성능 측정은
실행하지 않았다. 특히 DX12 fence 완료와 실제 RT 오류/종료 경로는 Windows 검증이 필요하다.

### CPU 시작·종료와 진행 중인 구간

- 녹화하지 않은 scope도 논리 스택에 제 자리를 갖는다. 녹화 전 A 안에서 녹화 후 B를
  열었을 때 B의 End가 A의 skip을 소비하지 않는다. 기록 깊이는 현재 세대에 기록된
  조상만 센다. 깊이 64를 넘은 scope는 안쪽 overflow로만 예약하고 손실에 포함한다.
- Begin의 세대와 admission을 고정한다. Stop된 세대의 늦은 Begin/instant는 받지 않고,
  이전 세대의 End와 GPU token은 새 녹화에 섞지 않는다. 녹화 전에 관측하지 않은 A의
  이름·시작 시간을 만들어 내지는 않는다.
- 활성 scope의 시작·이름·깊이·CPU context는 고정 크기 atomic 슬롯에 공개한다.
  수집기는 다른 스레드의 TLS 스택이나 쓰는 중인 청크를 읽지 않는다. 슬롯마다 재사용하지
  않는 token이 있고, 주인의 End와 수집기의 종료 절단 중 CAS에 성공한 한쪽만 기록한다.
- live snapshot은 공개된 열린 구간을 현재 상세 창 끝까지 투영한다. 이 구간에는
  truncated_end가 있으며 실제 종료를 관측했다는 뜻이 아니다. 투영은 불변 보기 사본에만
  들어가고, 링이나 연속 파일에 반복 저장하지 않는다. 메모리 예산을 넘는 투영은 생략한다.

### Stop과 GPU 소유권

Stop은 CPU 신규 admission을 닫고 하나의 배타적 종료 경계(QPC 한 단위 뒤)를 정한다.
Starting 중 받은 Stop도 시작 적용 뒤 처리한다. UI/GT/RT는 꼬리 응답이나 디스크를 기다리지
않는다. 전용 collector가 CPU freeze 응답과 이미 승인한 GPU 제출의 종료를 최대
stop_drain_timeout_ms(기본 250 ms, 0..5000 ms) 동안 확인한다. 대기 횟수/yield로 시간을
추정하지 않는다. 기록 시작/종료 제어는 직렬로 적용하며 이전 writer의 Finalized/Failed 전에
다음 Record를 받지 않는다.

GPU backend는 제출을 열 때 승인받은 capture generation을 token에 보관하고,
해당 제출의 마지막 span 청크를 공개한 뒤 정확히 한 번 완료/실패를 알린다. Stop 뒤에는
새 제출을 승인하지 않지만 이미 승인한 제출의 결과는 drain 동안 받는다. deadline 뒤에
남은 제출 수는 불완전 사유이며, 이를 late_gpu_spans와 같은 뜻으로 읽지 않는다.

- DX12: 기존 RT 소유의 fence 완료 polling/CollectCompletedDisplays에서 수집한다.
  Stop을 위해 GT/collector가 query를 읽거나 GPU를 기다리는 경로를 추가하지 않는다.
  abort, 슬롯 폐기/재사용, query 부족, 정렬 실패, device/RT 종료도 미완료로 퇴역시킨다.
- Vulkan live: 현재 timestamp 계측이 없으므로 명시적으로 unavailable을 남긴다.
  별도의 offline 진단 profiler를 live Stop에서 호출하지 않는다. CPU 자료는 계속 쓸 수
  있지만, GPU까지 온전한 캡처라고 표시하지 않는다.
- 승인된 GPU 제출이 0이면 coverage는 미관측이다. backend 지원, GPU 무작업 또는
  0 ms 실행을 추정하지 않는다. 현재 live GPU sink에는 별도 사용자 enable 토글이 없다.

미응답 CPU의 공개된 열린 구간은 collector가 emission 권한을 가져와 Stop 시각에서
truncated_end로 보존한다. producer가 이후 재개되어도 같은 scope를 다시 적지 않는다.
전역 Stop 경계와 개별 freeze 통지 사이에 종료된 CPU span은 collector가 새로 받은
봉인 페이지를 아직 단독 소유할 때 같은 Stop 경계로 자른다. GPU timestamp는 자르지
않고, 이미 불변 캡처가 참조하는 페이지도 고치지 않는다. 이 통지 지연 interleaving은
정적 검토만 했으며 실행으로 검증하지 않았다.
아직 게시되지 않은 완료 이벤트나 Begin 중간에서 멈춘 슬롯까지 복구할 수 있다는 보장은
없다. 해당 스레드는 미응답으로 남고 캡처도 incomplete다. 메모리 풀 소진으로 진단 이벤트
자체를 넣지 못하면 source loss를 올린다.

### CEPROF v3 진단 표현

파일 version, 레코드 종류, footer, 이벤트 62바이트, counter 34바이트 및 CRC 계약을
바꾸지 않는다. 기존 descriptor/instant/counter 표현만 사용하므로 v1/v2/v3 reader 경로를
유지한다. 이전 v3 reader도 이름과 값을 볼 수 있다.

마지막 tail frame에는 다음 자료를 보존한다.

- Capture.Incomplete.CpuProducerTimeout instant: 실제 producer의 thread slot과 이름
- Capture.Incomplete.Gpu: <reason> instant: 마지막 대표 GPU 실패 이유 또는 drain timeout
- Capture.UnackedCpuStreams / Capture.OpenScopesAtStop
- Capture.AdmittedGpuSubmissions / Capture.PendingGpuSubmissions / Capture.FailedGpuSubmissions
- Capture.StopDrainMilliseconds

OpenScopesAtStop은 실제 종료를 못 본 절단 구간의 수다. CPU timeout instant가 있는
스레드만 미응답이며, 모든 truncated_end를 timeout이나 완료 시간으로 해석하지 않는다.
파일의 앞/중간 상세 창에는 마지막 진단이 없을 수 있으므로 UI는 마지막 프레임으로
이동하는 동작을 제공한다. 없는 진단을 0이나 complete로 해석하지 않는다.

메모리 캡처의 complete도 미응답/미회수 GPU뿐 아니라 알려진 source/late/프레임 경계
손실을 반영한다. writer의 큐/디스크 손실과 Finalized는 별도 상태이며 파일 footer가 함께
검증한다. 정상 파일 마무리 이후의 이벤트를 다시 녹화된 것으로 주장하지 않는다.

추가한 회귀 소스는 disabled parent, 재귀/깊이 초과, Stop/새 세대, 주인 종료,
미응답 producer의 공개 시작점과 CAS 선점 양방향, 늦은 GPU 결과/옛 token,
v3 불완전 EOF 복구와 legacy reader를 다룬다. 이 목록은 실행 통과 목록이 아니다.
