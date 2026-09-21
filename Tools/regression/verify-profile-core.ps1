[CmdletBinding()]
param([ValidateSet('Debug','Release','All')][string]$Configuration = 'All')

# PHASE 14 P1+P2 — 새 프로파일러 코어의 계약 검사.
#
# 엔진을 띄우지 않는다. EngineDiagnostics 가 ProjectReference 0 의 독립
# 라이브러리이고 서비스가 인스턴스로 서므로 코어만 링크해 초 단위로 돈다.
# 옛 코어는 전역 싱글톤 + 함수 지역 static thread_local 이라 이런 검사가
# 불가능했고, 그래서 selftest 가 라이브 캡처의 프레임 경계를 직접 넘겨
# 교란해야 했다(그 교란 때문에 stats 를 selftest 직후에 재면 포화로 보인다).
#
# ★ 변이를 함께 돌린다. 초록인 검사는 그 자체로는 아무것도 증명하지 않는다 —
#   틀린 코어에서 붉어지는 것을 봐야 이빨이 있다.

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$repo = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$vcvars = 'C:/Program Files/Microsoft Visual Studio/18/Community/VC/Auxiliary/Build/vcvars64.bat'
$core = Join-Path $repo 'Engine/EngineDiagnostics'
$configs = if ($Configuration -eq 'All') { @('Debug','Release') } else { @($Configuration) }

# 변이: 코어의 계약을 하나씩 깨뜨려 각 검사가 실제로 그것을 잡는지 본다.
# 앵커가 소스에서 사라지면 '대상이 없다' 로 붉어진다 — 검사가 낡은 것을 그때 안다.
$mutations = @(
    @{
        Name   = 'close-open-scopes'
        File   = 'ProfileThreadStream.cpp'
        Old    = "`t`tseal_current();`r`n`r`n`t`t// 주인이 직접 봉인했으니"
        New    = "`t`twhile (m_depth > 0) { end_scope(0); }`r`n`t`tseal_current();`r`n`r`n`t`t// 주인이 직접 봉인했으니"
        Expect = 'cross-frame/'
        Why    = '프레임 경계에서 열린 스코프를 닫으면(옛 코어가 그랬다) 프레임을 넘는 구간을 잃는다'
    },
    @{
        Name   = 'silent-drop'
        File   = 'ProfileThreadStream.cpp'
        Old    = "`t`thonor_seal_request();`r`n`r`n`t`tif (!ensure_chunk())`r`n`t`t{`r`n`t`t`tm_droppedEvents.fetch_add(1, std::memory_order_relaxed);`r`n"
        New    = "`t`thonor_seal_request();`r`n`r`n`t`tif (!ensure_chunk())`r`n`t`t{`r`n"
        Expect = 'overflow/'
        Why    = '잃은 이벤트를 세지 않으면 프레임이 정상인 척한다'
    },
    @{
        # 모든 마커가 같은 id 를 받게 한다. '이름마다 구분되는 id' 가 이 코어의
        # 계약이고, 그것이 깨지면 어느 구간이 무엇인지 알 수 없게 된다.
        #
        # ★ 처음에는 intern_marker 의 중복 제거(return found->second)를 지우는
        #   변이를 썼는데 통과했다. 같은 이름의 marker_slot 은 inline 변수라
        #   한 번만 초기화되므로 id 는 그래도 안정적이고, 중복 제거는 지금
        #   **이중 안전망**이지 계약을 지탱하는 코드가 아니었기 때문이다.
        #   (동적 이름이 들어오는 P5 에서 그 경로가 비로소 자극된다.)
        Name   = 'marker-merge'
        File   = 'ProfileMarker.cpp'
        Old    = "`t`t`tconst marker_id id = static_cast<marker_id>(reg.descs.size());"
        New    = "`t`t`tconst marker_id id = 1;"
        Expect = 'marker/'
        Why    = '모든 이름이 한 id 로 뭉치면 어느 구간이 무엇인지 알 수 없다'
    },

    # ── PHASE 14 P3 집계 ────────────────────────────────────────────────────
    @{
        # 부모를 찾지 않고 전부 루트로 접는다. 트리가 사라지면 자식의 시간이
        # 루트 합에 두 번 들어가 프레임 예산이 통째로 거짓이 된다.
        Name   = 'aggregate-flatten'
        File   = 'ProfileAggregate.cpp'
        Old    = "`t`t`tconst node_key key{ stack.empty() ? 0u : stack.back(),"
        New    = "`t`t`tconst node_key key{ 0u,"
        Expect = 'aggregate/'
        Why    = '깊이를 무시하고 접으면 자식이 루트로 올라와 트리가 사라진다'
    },
    @{
        # self 를 total 그대로 둔다. 표의 모든 줄이 자기 자식의 시간을 제 것으로
        # 주장하게 되고, self 로 병목을 찾는 일이 전부 틀어진다.
        Name   = 'aggregate-self-as-total'
        File   = 'ProfileAggregate.cpp'
        Old    = "`t`t`tnode.self_ticks = (node.total_ticks > childTicks[i])"
        New    = "`t`t`tnode.self_ticks = node.total_ticks; if (false) node.self_ticks = (node.total_ticks > childTicks[i])"
        Expect = 'aggregate/'
        Why    = 'self 에서 자식을 빼지 않으면 병목을 self 로 찾는 일이 전부 틀어진다'
    },
    @{
        # 정렬을 걷는다. 이벤트는 **끝난 순서**로 기록되므로 정렬하지 않으면
        # 자식이 부모보다 먼저 나와 트리가 뒤집힌다 — 이 코어에서 가장 틀리기
        # 쉬운 가정이 그것이라 변이로 못 박는다.
        Name   = 'aggregate-unsorted'
        File   = 'ProfileAggregate.cpp'
        Old    = "`t`tstd::sort(events.begin(), events.end(), precedes);"
        New    = "`t`tif (events.size() > 1000000) std::sort(events.begin(), events.end(), precedes);"
        Expect = 'aggregate/'
        Why    = '이벤트는 끝난 순서로 들어오므로 정렬 없이는 자식이 부모보다 먼저 나온다'
    },

    # ── PHASE 14 P3 reader ──────────────────────────────────────────────────
    @{
        # Live Follow 를 무시하고 언제나 최신으로 간다. 스파이크를 붙잡아 둘 수
        # 없게 되는데, 캡처를 다시 받기 전까지는 화면상 아무 차이도 없다.
        Name   = 'reader-follow-always'
        File   = 'ProfileReader.cpp'
        Old    = "`t`tif (m_liveFollow)"
        New    = "`t`tif (true)"
        Expect = 'reader-follow/'
        Why    = '따라가기를 꺼도 최신으로 점프하면 붙잡아 두겠다는 약속이 깨진다'
    },
    @{
        # 선택이 바뀌어도 집계 캐시를 그대로 둔다. 과거 프레임을 골라도 화면은
        # 이전 프레임을 계속 보여 주는데, 숫자가 그럴듯해서 눈으로는 모른다.
        Name   = 'reader-stale-cache'
        File   = 'ProfileReader.cpp'
        Old    = "`t`t`tm_aggregateValid = false;`r`n`t`t`tm_viewValid = false;`r`n`t`t}`r`n`t}`r`n`r`n`tvoid capture_reader::select_latest()"
        New    = "`t`t`tm_aggregateValid = true;`r`n`t`t`tm_viewValid = false;`r`n`t`t}`r`n`t}`r`n`r`n`tvoid capture_reader::select_latest()"
        Expect = 'reader/'
        Why    = '선택이 바뀌어도 캐시를 안 버리면 다른 프레임의 숫자를 계속 보여 준다'
    },

    # ── PHASE 14 P3 Timeline ────────────────────────────────────────────────
    @{
        # 시야를 구간 안으로 자르지 않는다. 멀리 밀면 빈 화면이 나오고,
        # 그때 사용자는 계측이 없다고 읽는다 — 화면만 봐서는 못 가린다.
        Name   = 'timeline-view-unclamped'
        File   = 'ProfileReader.cpp'
        Old    = "`t`tif (m_viewBegin < low)"
        New    = "`t`tif (false)"
        Expect = 'timeline-view/'
        Why    = '시야가 구간 밖으로 나가면 빈 화면이 나오고 계측이 없는 것처럼 보인다'
    },
    @{
        # 선택이 바뀌어도 시야를 그대로 둔다. 다른 프레임을 골랐는데 전에
        # 보던 tick 을 계속 보므로 타임라인이 빈다.
        Name   = 'timeline-view-kept'
        File   = 'ProfileReader.cpp'
        Old    = "`t`t`tm_aggregateValid = false;`r`n`t`t`tm_viewValid = false;"
        New    = "`t`t`tm_aggregateValid = false;"
        Expect = 'timeline-view/reset-on-select'
        Why    = '선택이 바뀌어도 시야를 안 되돌리면 다른 프레임에서 빈 타임라인을 본다'
    },

    # ── 녹화 경계를 넘는 스코프 ────────────────────────────────────
    @{
        # 여는 쪽을 건너뛰면서 짝을 예약하지 않는다. 얼린 채 열린 구간의 짝이
        # 나중에 스택에서 남의 구간을 닫는다.
        Name   = 'scope-skip-unpaired'
        File   = 'ProfileService.cpp'
        Old    = "`t`t`tstream->skip_scope();`r`n`t`t`treturn;"
        New    = "`t`t`treturn;"
        Expect = 'state-change/'
        Why    = '여는 쪽만 건너뛰면 그 짝이 스택에서 남의 구간을 닫는다'
    },
    @{
        # 닫는 쪽에도 상태 관문을 다시 건다. 얼린 뒤에 닫힌 구간이 스택에 남아
        # 그 뒤의 깊이가 한 칸씩 밀린다.
        #
        # ★ 이 변이는 **아무것도 실패시키지 않는다.** 불균형 계수기는 0 인
        #   채로 깊이만 밀리므로, 계수기만 보는 단정은 이것을 못 잡는다. Expect 를
        #   깊이 단정에 묶어 둔 것이 그 이유다.
        Name   = 'scope-end-gated'
        File   = 'ProfileService.cpp'
        Old    = "`t`tthread_stream* stream = tls_stream();`r`n`t`tif (!stream)"
        New    = "`t`tif (m_state.load(std::memory_order_relaxed) != recorder_state::recording)`r`n`t`t{`r`n`t`t`treturn;`r`n`t`t}`r`n`t`tthread_stream* stream = tls_stream();`r`n`t`tif (!stream)"
        # pause 가 열린 구간을 잘라 짝을 예약하게 되면서(§0.5.16) 안쪽 깊이는
        # 그 예약이 되살린다. 바깥쪽이 여전히 밀리므로 이빨은 그대로다.
        Expect = 'state-change/outer-depth'
        Why    = '닫는 쪽을 얼릴 수 있으면 스택에 칸이 남아 그 뒤의 깊이가 전부 밀린다'
    },

    # ── 얼린 캡처의 꼬리 ──────────────────────────────────────────
    @{
        # pause 가 열린 구간을 자르지 않던 때로 되돌린다.
        Name   = 'pause-keeps-open-scope'
        File   = 'ProfileThreadStream.cpp'
        Old    = "`t`tif (freeze != 0)"
        New    = "`t`tif (false)"
        # pause 를 부른 스레드는 pause 가 직접 자르므로 이 가지를 안 탄다.
        # 계속 적는 워커만 여기를 지난다.
        Expect = 'pause-worker/present'
        Why    = 'pause 에는 다음 프레임이 없어 여기서 남기지 않으면 그 구간은 영영 사라진다'
    },
    @{
        # 잘랐다고 표시하지 않는다. 진짜 종료가 같은 구간을 한 번 더 적는다.
        Name   = 'pause-truncate-unmarked'
        File   = 'ProfileThreadStream.cpp'
        Old    = "`t`t`tscope.emitted = true;"
        New    = ""
        Expect = 'pause-open/once'
        Why    = '표시가 없으면 잘린 구간이 진짜 종료에서 한 번 더 기록된다'
    },
    @{
        # 잘린 구간의 자리를 비운다(옛 개수 예약 모델).
        Name   = 'truncate-pops-stack'
        File   = 'ProfileThreadStream.cpp'
        Old    = "`t`t`tif (scope.emitted) continue;`r`n`t`t`tif (scope.generation != generation) continue;"
        New    = "`t`t`tif (scope.emitted) continue;`r`n`t`t`tif (scope.generation != generation) continue;`r`n`t`t`t--m_depth;`r`n`t`t`t++m_skippedDepth;"
        Expect = 'resume-pair/closed-on-time'
        Why    = '자리를 비우면 다시 녹화한 뒤의 새 구간의 종료가 그 예약을 먼저 먹는다'
    },
    @{
        # 스코프에 찍은 세대를 보지 않는다.
        Name   = 'scope-ignores-generation'
        File   = 'ProfileThreadStream.cpp'
        Old    = "`t`tif (scope.generation != m_generation.load(std::memory_order_acquire))"
        New    = "`t`tif (false)"
        Expect = 'clear-open/dropped'
        Why    = '세대가 청크에만 있으면 Clear 전에 열린 구간이 새 캡처로 돌아온다'
    },
    @{
        # Clear 가 봉인을 청하지 않는다. 쓰던 청크가 그대로 이어진다.
        Name   = 'clear-without-seal'
        File   = 'ProfileService.cpp'
        Old    = "`t`t`t`tentry.stream->request_seal();"
        New    = ""
        Expect = 'clear-new/kept'
        Why    = '쓰던 청크를 끊지 않으면 Clear 뒤에 적은 것까지 옛 세대로 버려진다'
    },
    # ★ 여기에 있던 'ack-before-seal'(재진입 가드 제거)과 그 후속
    #   'freeze-ack-unconditional'(얼림 조건 제거)은 **이빨이 없어 뺐다.**
    #
    #   얼림 응답을 평범한 봉인과 가르면서, 재진입한 안쪽 호출은 freeze 를
    #   이미 0 으로 교환한 뒤라 얼림 응답을 올리지 않게 됐다. 남은 두 절 —
    #   m_inHonor 재진입 가드와 `if (freeze != 0)` 조건 — 은 **좁은 끼어듦
    #   순서**를 막는 것이라, 걷어내도 그 순서가 실제로 겹쳐야만 붉어진다.
    #   실측에서 두 변이 모두 통과했다. 회차에 기대는 판정을 초록으로 세워
    #   두면 "이빨이 있다" 는 거짓말이 되므로 빼고, 대신 ack-delivery/rounds
    #   가 24 회를 돌아 경험적으로 덮는다.
    @{
        # 얼린 캡처가 언제나 온전하다고 말한다.
        Name   = 'freeze-always-complete'
        File   = 'ProfileService.cpp'
        Old    = "`t`tcapture_session_ptr frozen = m_ring.freeze(threads, 0 == unacked, unacked);"
        New    = "`t`tcapture_session_ptr frozen = m_ring.freeze(threads, true, 0);"
        Expect = 'incomplete/flag'
        Why    = '세는 것과 판정하는 것은 다르다 - 미응답을 덮으면 빠진 꼬리가 조용해 보인다'
    },
    @{
        # 얼리기 전에 남은 프레임을 닫지 않는다.
        Name   = 'pause-drops-pending'
        File   = 'ProfileService.cpp'
        Old    = "`t`tif (m_ring.has_pending_events())"
        New    = "`t`tif (false)"
        Expect = 'pause-open/present'
        Why    = 'freeze() 는 닫힌 프레임만 보므로 마지막 프레임을 닫지 않으면 꼬리가 통째로 빠진다'
    },

    # ── 세대와 늦은 CPU 귀속 ──────────────────────────────────────
    @{
        # Clear 가 세대를 올리지 않는다.
        Name   = 'clear-keeps-generation'
        File   = 'ProfileService.cpp'
        Old    = "`t`tconst std::uint64_t next = m_generation.fetch_add(1, std::memory_order_acq_rel) + 1;"
        New    = "`t`tconst std::uint64_t next = m_generation.load(std::memory_order_acquire);"
        Expect = 'clear-generation/dropped'
        Why    = '세대가 그대로면 Clear 전에 열린 청크가 새 녹화로 재유입된다'
    },
    @{
        # 늦게 온 CPU 구간을 수집한 프레임에 그냥 담는다.
        Name   = 'late-cpu-to-collecting-frame'
        File   = 'ProfileCapture.cpp'
        Old    = "`t`t`t`t`tif (place_by_tick(value)) continue;`r`n"
        New    = ""
        Expect = 'late-cpu/frame'
        Why    = '수집한 프레임에 담으면 잠든 워커의 구간이 깨어난 프레임의 일처럼 보인다'
    },

    # ── 단일 collector ────────────────────────────────────────────
    @{
        # 어느 스레드에서 불리든 그 자리에서 한다(예전 동작). 링을 수집기와
        # 다른 스레드가 함께 만진다.
        Name   = 'control-applies-inline'
        File   = 'ProfileService.cpp'
        Old    = "`t`tif (on_collector())"
        New    = "`t`tif (true)"
        Expect = 'control-thread/deferred'
        Why    = '링은 프레임 경계를 도는 스레드의 것이다 - 남이 직접 만지면 수집기와 겹친다'
    },
    @{
        # 부른 쪽이 적용될 때까지 기다리게 되돌린다(직전 판의 동작).
        #
        # ★ UI 는 씬 잠금을 쥔 채 부르고 수집기는 같은 잠금을 통과해야 이
        #   요청을 처리한다. 기다리는 순간 서로를 기다린다.
        Name   = 'control-waits-for-apply'
        File   = 'ProfileService.cpp'
        Old    = "`t`tm_controlDeferred.fetch_add(1, std::memory_order_relaxed);`r`n`t}"
        New    = "`t`tm_controlDeferred.fetch_add(1, std::memory_order_relaxed);`r`n`r`n`t`tconst std::uint64_t seq = m_controlEnqueued.load(std::memory_order_acquire);`r`n`t`tconst auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(2000);`r`n`t`twhile (m_controlApplied.load(std::memory_order_acquire) < seq`r`n`t`t       && std::chrono::steady_clock::now() < deadline)`r`n`t`t{`r`n`t`t`tstd::this_thread::yield();`r`n`t`t}`r`n`t}"
        Expect = 'pause-nonblocking/fast'
        Why    = '부른 쪽이 완료를 기다리면 UI 의 잠금과 수집기가 서로를 기다린다'
    },

    # ── 종료 소유권 ───────────────────────────────────────────────
    @{
        # 남의 스트림도 종료 스레드가 직접 닫는다(예전 동작).
        Name   = 'shutdown-finishes-foreign'
        File   = 'ProfileService.cpp'
        Old    = "`t`tconst bool owned = (entry.stream->owner_thread() == std::this_thread::get_id());"
        New    = "`t`tconst bool owned = true;"
        Expect = 'shutdown-retain/counted'
        Why    = '주인이 살아 있는데 남이 finish() 를 부르고 저장소까지 해제하면 쓰는 자리가 사라진다'
    },
    @{
        # thread_local 자리의 세대를 보지 않는다. 지난 서비스의 죽은 스트림을
        # 그대로 따라간다.
        #
        # ★ 이 변이는 **죽을 수 있다.** 해제된 스트림으로 쓰기 때문이다.
        Name   = 'tls-ignores-epoch'
        File   = 'ProfileService.cpp'
        Old    = "`t`treturn (slot.epoch == m_slotEpoch) ? slot.stream : nullptr;"
        New    = "`t`t(void)m_slotEpoch;`r`n`t`treturn slot.stream;"
        Expect = 'tls-epoch/'
        AllowCrash = $true
        Why    = '남의 스레드의 자리는 shutdown 이 끊을 수 없으므로 세대로 무효화해야 한다'
    },

    # ── 동시성 경계: 봉인은 주인만 한다 ───────────────────────────
    @{
        # 수집기가 남의 현재 청크를 직접 봉인하던 예전 동작으로 되돌린다.
        #
        # ★ 이 변이는 **죽는다.** writer 가 m_writer 로 쓰는 동안 수집기가 그
        #   포인터를 비우고 청크를 풀에 되돌리므로, 두 스레드가 같은 저장소를
        #   만진다. 실측에서 Debug·Release 모두 ACCESS_VIOLATION 이었다.
        #   그래서 stderr 의 표식이 아니라 충돌로 판정한다.
        Name   = 'collector-seals-others'
        File   = 'ProfileService.cpp'
        Old    = "`t`t`t`tif (entry.stream.get() == self)`r`n`t`t`t`t{`r`n`t`t`t`t`tentry.stream->publish_frame();`r`n`t`t`t`t}`r`n`t`t`t`telse`r`n`t`t`t`t{`r`n`t`t`t`t`tentry.stream->request_seal();`r`n`t`t`t`t}"
        New    = "`t`t`t`t(void)self;`r`n`t`t`t`tentry.stream->publish_frame();"
        # ★ 이제는 죽지 않는다. 소유를 강제하므로 남의 호출은 거절되고 세어진다.
        #   충돌 판정은 회차에 기대는 자였다 — 같은 변이가 어떤 실행에서는
        #   겹치지 않아 초록으로 지나갔다. 계수는 겹치든 말든 선다.
        Expect = 'concurrent-publish/owned'
        Why    = '남의 청크를 봉인하면 쓰기와 겹쳐 이벤트를 잃거나 프로세스가 죽는다'
    },

    # ── GPU 레인: 늦게 온 구간이 제 프레임 칸으로 가는가 ───────────
    @{
        # 늦게 온 것을 CPU 경로와 똑같이 **수집한 프레임**에 담는다. 실측에서
        # 제출→수집이 최대 54.6 ms 였으므로 GPU 일이 세 칸 뒤에 그려진다.
        Name   = 'gpu-span-to-pending'
        File   = 'ProfileCapture.cpp'
        Old    = "`t`t`tif (sealed_list->late_ingest)"
        New    = "`t`t`tif (false)"
        # 늦은 CPU 귀속이 생기면서(§0.5.16) 이 구간은 **끝난 시각** 규칙을 타고
        # 엉뚱한 칸에 앉는다. GPU 는 제출 프레임이 제 자리라 여전히 붉다.
        Expect = 'gpu-deferred/placed'
        Why    = '수집한 프레임에 담으면 GPU 일이 제 프레임보다 뒤에 그려진다'
    },
    @{
        # 아직 안 닫힌 프레임의 것을 기다리지 않고 곧바로 버린다. 그 프레임이
        # 곧 닫히는데도 구간이 사라지므로 레인에 구멍이 생긴다.
        Name   = 'gpu-span-no-defer'
        File   = 'ProfileCapture.cpp'
        Old    = "`t`tif (m_deferredSpans.size() >= kMaxDeferredSpans)"
        New    = "`t`tif (true)"
        Expect = 'gpu-deferred/placed'
        Why    = '닫히기 전에 온 구간을 기다리지 않으면 그 프레임의 레인이 빈다'
    },
    @{
        # 갈 곳이 없어 버린 것을 세지 않는다. "레인이 비었다" 와 "늦어서
        # 잃었다" 가 구분되지 않는다 — 빈 집합을 성공으로 읽는 그 양식이다.
        Name   = 'gpu-span-silent-drop'
        File   = 'ProfileCapture.cpp'
        Old    = "`t`t`t++m_lateSpansDropped;`r`n`t`t`treturn;`r`n`t`t}`r`n`r`n`t`t// 그 프레임이 아직 안 닫혔다."
        New    = "`t`t`treturn;`r`n`t`t}`r`n`r`n`t`t// 그 프레임이 아직 안 닫혔다."
        Expect = 'gpu-dropped/counted'
        Why    = '버린 것을 세지 않으면 빈 레인과 잃은 레인이 같아 보인다'
    },

    # ── 창이 매 프레임 부르는 따라가기 규칙 ────────────────────────
    @{
        # 한 번 집으면 끝이다. Live Follow 를 켜 두어도 새로 얼린 것으로 가지
        # 않으므로, 두 번째 pause 부터는 화면이 옆에 멈춰 선다.
        Name   = 'reader-sync-once'
        File   = 'ProfileReader.cpp'
        Old    = "`t`tif (m_capture && !m_liveFollow)"
        New    = "`t`tif (m_capture)"
        Expect = 'reader-sync/follow'
        Why    = '한 번만 집으면 Live Follow 가 약속한 것을 지키지 못한다'
    },
    @{
        # 같은 것을 받아도 매번 갈아태다. 창이 매 프레임 부르므로 선택과 시야가
        # 매 프레임 초기화되고 접은 결과가 매 프레임 버려진다.
        Name   = 'reader-sync-always'
        File   = 'ProfileReader.cpp'
        Old    = "`t`tif (latest.get() == m_capture.get())"
        New    = "`t`tif (false)"
        Expect = 'reader-sync/same'
        Why    = '같은 것을 매 프레임 다시 집으면 선택과 시야가 매 프레임 초기화된다'
    }
)

$sources = @(
    (Join-Path $core 'ProfileMarker.cpp'),
    (Join-Path $core 'ProfileThreadStream.cpp'),
    (Join-Path $core 'ProfileCapture.cpp'),
    (Join-Path $core 'ProfileAggregate.cpp'),
    (Join-Path $core 'ProfileReader.cpp'),
    (Join-Path $core 'ProfileService.cpp'),
    (Join-Path $PSScriptRoot 'profile_core_probe.cpp')
)

function Invoke-Probe {
    param(
        [string]   $Config,
        [string]   $Name,
        [string[]] $SourceList,
        [string]   $OutDir
    )

    $exe = Join-Path $OutDir "$Name.exe"
    $flags = if ($Config -eq 'Debug') { '/MDd /Od /RTC1 /D_DEBUG' } else { '/MD /O2 /DNDEBUG' }
    $quoted = ($SourceList | ForEach-Object { '"' + $_ + '"' }) -join ' '

    # /WX — 새 코어는 경고 0 이 계약이다.
    $command = 'call "' + $vcvars + '" >nul && cl /nologo /EHsc /std:c++latest /utf-8 /W4 /WX ' +
        $flags + ' /I"' + $core + '" /Fo"' + $OutDir + '/" /Fd"' + (Join-Path $OutDir "$Name.pdb") +
        '" /Fe"' + $exe + '" ' + $quoted

    $log = & $env:ComSpec /d /s /c $command 2>&1
    if ($LASTEXITCODE -ne 0) {
        return @{ Compiled = $false; ExitCode = -1; StdOut = ''; StdErr = ($log -join "`n") }
    }

    $outFile = Join-Path $OutDir "$Name.out"
    $errFile = Join-Path $OutDir "$Name.err"
    $proc = Start-Process -FilePath $exe -WindowStyle Hidden -PassThru `
        -RedirectStandardOutput $outFile -RedirectStandardError $errFile
    if (-not $proc.WaitForExit(60000)) {
        $proc.Kill()
        $proc.WaitForExit()
        throw "$Config $Name timed out"
    }
    $proc.WaitForExit()

    return @{
        Compiled = $true
        ExitCode = $proc.ExitCode
        StdOut   = (Get-Content -LiteralPath $outFile -Raw -ErrorAction SilentlyContinue)
        StdErr   = (Get-Content -LiteralPath $errFile -Raw -ErrorAction SilentlyContinue)
    }
}

$failures = New-Object System.Collections.Generic.List[string]

foreach ($config in $configs) {
    $out = Join-Path $repo "Build/Obj/ProfileCore/$config"
    New-Item -ItemType Directory -Force $out | Out-Null

    # --- 기준: 손대지 않은 코어 ---------------------------------------
    $baseline = Invoke-Probe -Config $config -Name 'profile-core' -SourceList $sources -OutDir $out
    if (-not $baseline.Compiled) {
        $failures.Add("$config baseline 컴파일 실패 (경고를 오류로 다룬다)`n$($baseline.StdErr)")
        continue
    }
    if ($baseline.ExitCode -ne 0) {
        $failures.Add("$config baseline 실패 (exit $($baseline.ExitCode))`n$($baseline.StdErr)")
        continue
    }
    if ($baseline.StdOut -notmatch 'PROFILE_CORE_OK=true') {
        $failures.Add("$config baseline 성공 마커가 없다")
        continue
    }
    $summary = ($baseline.StdOut -split "`n" | Where-Object { $_ -match 'checks' } | Select-Object -First 1)
    Write-Host ("[OK]   $config baseline — " + $summary.Trim())

    # --- 변이: 계약을 깨뜨리면 붉어져야 한다 ---------------------------
    foreach ($mutation in $mutations) {
        $original = Join-Path $core $mutation.File
        $text = [IO.File]::ReadAllText($original)
        if (-not $text.Contains($mutation.Old)) {
            $failures.Add("$config 변이 '$($mutation.Name)' 의 대상이 소스에 없다 — 검사가 낡았다")
            continue
        }

        $mutantFile = Join-Path $out ("Mutant-" + $mutation.Name + '-' + $mutation.File)
        [IO.File]::WriteAllText($mutantFile, $text.Replace($mutation.Old, $mutation.New))

        $mutantSources = $sources | ForEach-Object {
            if ($_ -eq $original) { $mutantFile } else { $_ }
        }

        $result = Invoke-Probe -Config $config -Name ("mutant-" + $mutation.Name) -SourceList $mutantSources -OutDir $out
        if (-not $result.Compiled) {
            $failures.Add("$config 변이 '$($mutation.Name)' 가 컴파일되지 않는다 — 변이가 대상을 잘못 짚었다`n$($result.StdErr)")
            continue
        }
        if ($result.ExitCode -eq 0) {
            $failures.Add("$config 변이 '$($mutation.Name)' 가 통과했다 — 검사에 이빨이 없다. $($mutation.Why)")
            continue
        }
        # ★ 어떤 결함은 **단정이 돌기 전에 프로세스를 죽인다.** 동시성 경계가
        #   그렇다 — 봉인과 쓰기가 겹치면 stderr 에 한 줄도 남기지 못하고
        #   ACCESS_VIOLATION 으로 끝난다. 그때는 충돌 자체가 붉음이다.
        #
        #   AllowCrash 를 적은 변이에만 허용한다. 모든 변이에 열어 두면
        #   "컴파일은 됐는데 엉뚱한 데서 죽었다" 가 통과로 읽힌다.
        $crashed = ($result.ExitCode -lt 0)
        if ($crashed -and $mutation.ContainsKey('AllowCrash') -and $mutation.AllowCrash) {
            Write-Host ("[OK]   $config 변이 '" + $mutation.Name +
                        "' — 충돌로 잡았다 (exit " + $result.ExitCode + ')')
            continue
        }
        if ($result.StdErr -notmatch [regex]::Escape($mutation.Expect)) {
            $failures.Add("$config 변이 '$($mutation.Name)' 가 붉어졌지만 다른 곳에서다. 기대='$($mutation.Expect)'`n$($result.StdErr)")
            continue
        }
        Write-Host ("[OK]   $config 변이 '" + $mutation.Name + "' — " + $mutation.Expect + ' 가 잡았다')
    }
}

Write-Host ''
if ($failures.Count -gt 0) {
    Write-Host '-- 판정 -----------------------------'
    foreach ($item in $failures) { Write-Host "[FAIL] $item" -ForegroundColor Red }
    Write-Host ("프로파일러 코어 검사 실패 " + $failures.Count + '건')
    exit 1
}

Write-Host '-- 판정 -----------------------------'
Write-Host ("프로파일러 코어 통과 — 계약이 서고, 변이 {0} 이 각각 제 검사에서 붉어진다" -f $mutations.Count)

# 종료 코드를 명시한다. 판정이 종료 코드뿐인 집중 검사 방식에서는 성공
# 경로가 남의 $LASTEXITCODE 를 흘리면 게이트가 판정 능력을 잃는다
# (verify-editor-icon-resources.ps1 이 git grep 의 1 을 흘리고 있었다).
exit 0
