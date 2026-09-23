[CmdletBinding()]
param(
    [ValidateSet('Debug','Release','All')][string]$Configuration = 'All',
    # 돌릴 변이 이름(와일드카드). 비우면 전부다. 변이마다 코어를 통째로 다시
    # 컴파일하므로 전부는 구성당 수십 분이다 — 바꾼 계약의 변이만 골라 돌린다.
    # 예: -Only 'reader-open-*','capture-file-*'
    #
    # ★ 이름이 `$Mutation` 이면 안 된다. PowerShell 변수는 대소문자를 안 가려
    #   아래 foreach 의 `$mutation` 과 같은 변수가 되고, [string[]] 형 제약이
    #   변이 표를 문자열로 바꿔 버린다.
    [string[]]$Only = @()
)

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
        Old    = "`t`t`tm_aggregateValid = false;`r`n`t`t}`r`n`t}`r`n`r`n`tvoid capture_reader::select_latest()"
        New    = "`t`t`tm_aggregateValid = true;`r`n`t`t}`r`n`t}`r`n`r`n`tvoid capture_reader::select_latest()"
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
        # 고를 때마다 시야를 되돌린다. 기준이 창으로 옮겨진 뒤로는 이것이
        # 퇴행이다 — 확대해 둔 것이 클릭 한 번에 풀린다.
        Name   = 'timeline-view-reset-on-select'
        File   = 'ProfileReader.cpp'
        Old    = "`t`t`t// ★ 시야는 건드리지 않는다."
        New    = "`t`t`tm_viewValid = false;`r`n`t`t`t// ★ 시야는 건드리지 않는다."
        Expect = 'timeline-view/kept-on-select'
        Why    = '고를 때마다 시야가 돌아가면 확대가 클릭 한 번에 풀린다'
    },
    @{
        # 시야를 창이 아니라 선택 구간으로 세운다. 지금 도구가 그러했다 —
        # 위 그래프가 244 프레임을 그리는 동안 아래는 한 칸만 그렸다.
        Name   = 'timeline-view-follows-selection'
        File   = 'ProfileReader.cpp'
        Old    = "`t`tconst frame_aggregate& folded = window_aggregate();`r`n`t`tm_viewBegin = folded.tick_begin();"
        New    = "`t`tconst frame_aggregate& folded = aggregate();`r`n`t`tm_viewBegin = folded.tick_begin();"
        # ★ 예측은 kept-on-select 였는데 실제로는 이 절이 먼저 붉어진다 —
        #   시야가 선택을 따르는 순간 **처음 서는 자리부터** 창과 어긋난다.
        Expect = 'timeline-view/initial-end'
        Why    = '시야가 선택을 따르면 타임라인이 그래프와 다른 범위를 말한다'
    },
    @{
        # 창 집계를 캐시에서 다시 안 접는다. 창이 미끄러져도 지난 창의
        # 구간을 계속 그린다.
        Name   = 'window-aggregate-stale'
        File   = 'ProfileReader.cpp'
        Old    = "`t`t    && m_windowAggregateFirst == first && m_windowAggregateLast == last)"
        New    = "`t`t    && m_windowAggregateFirst <= first && m_windowAggregateLast >= last)"
        Expect = 'window/follows-span'
        Why    = '창이 미끄러져도 안 다시 접으면 지난 구간을 계속 그린다'
    },

    # ── P6 캡처의 어휘 ─────────────────────────────────────────────
    #
    # ★ 이름을 전역 registry 로 풀면 파일에서 읽은 캡처가 **남의 빌드의
    #   이름**을 그린다. 화면에는 멀쩡한 글자가 나오므로 눈으로는 못 잡는다.
    @{
        # 얼릴 때 어휘를 안 싣는다. 캡처가 제 이름을 말할 수 없게 된다.
        Name   = 'capture-vocabulary-empty'
        File   = 'ProfileCapture.cpp'
        Old    = "`t`t`tstd::move(frames), std::move(thread_list), snapshot_markers(),"
        New    = "`t`t`tstd::move(frames), std::move(thread_list), std::vector<capture_marker>{},"
        Expect = 'vocabulary/name-outer'
        Why    = '어휘를 안 실으면 캡처가 제 이름을 말하지 못한다'
    },

    @{
        # 어휘의 크기를 **전역 표**에서 읽는다. 얼린 뒤 등록된 마커가 이
        # 캡처에 섞여 보이고, 그것이 곧 "복사하지 않았다" 의 증상이다.
        Name   = 'capture-vocabulary-follows-registry'
        File   = 'ProfileCapture.cpp'
        Old    = "`t`treturn static_cast<std::uint32_t>(m_markers.size());"
        New    = "`t`treturn registered_marker_count();"
        Expect = 'vocabulary/frozen'
        Why    = '전역 수를 내면 얼린 뒤 등록한 마커가 이 캡처에 섞인다'
    },

    @{
        # 모르는 id 에 자리표 이름을 준다. "모른다" 가 화면에서 사라지고
        # 남의 캡처의 id 가 그럴듯한 글자로 그려진다.
        Name   = 'capture-vocabulary-unknown-named'
        File   = 'ProfileCapture.cpp'
        Old    = "`t`tstatic const capture_marker unknown{};"
        New    = "`t`tstatic const capture_marker unknown{ `"<invalid>`", {}, 0, marker_kind::cpu_scope };"
        Expect = 'vocabulary/unknown'
        Why    = '모르는 id 가 이름을 가지면 "모른다" 를 화면에서 잃는다'
    },

    @{
        # 얼릴 때 시계를 안 싣는다. 파일에서 읽은 캡처가 제 주파수를 모르게
        # 되고, 그러면 읽는 기계의 QPC 로 나누는 옛 길로 되돌아간다.
        Name   = 'capture-clock-dropped'
        File   = 'ProfileService.cpp'
        Old    = "`t`t`tthreads, capture_environment{ ticks_per_second() }, 0 == unacked, unacked);"
        New    = "`t`t`tthreads, capture_environment{}, 0 == unacked, unacked);"
        Expect = 'clock/carried'
        Why    = '시계를 안 실으면 캡처가 제 구간 길이를 말하지 못한다'
    },

    @{
        # 초를 밀리초로 안 바꾼다. 모든 구간이 1000 배 짧게 나오는데, 표의
        # 모양은 그대로라 "빨라졌다" 로 읽힌다.
        Name   = 'capture-clock-seconds'
        File   = 'ProfileCapture.cpp'
        Old    = "`t`treturn static_cast<double>(ticks) * 1000.0 /"
        New    = "`t`treturn static_cast<double>(ticks) * 1.0 /"
        Expect = 'clock/milliseconds'
        Why    = '초를 ms 로 안 바꾸면 모든 구간이 1000 배 짧게 나온다'
    },

    # ── P6-2 .ceprof ───────────────────────────────────────────────
    #
    # ★ 새 검사 45 개가 첫 컴파일에서 전부 초록이었다. 그것은 아무것도
    #   증명하지 않는다 — 아래가 각 절에 이빨이 있다는 증명이다.
    @{
        # CRC 를 안 본다. 망가진 몸통이 그대로 읽혀 틀린 값을 그린다.
        Name   = 'capture-file-skips-crc'
        File   = 'ProfileCaptureFile.cpp'
        Old    = "`t`t`tif (crc32(body) != entry.crc)"
        New    = "`t`t`tif (crc32(body) != crc32(body))"
        Expect = 'file-reject/checksum'
        Why    = 'CRC 를 안 보면 망가진 파일이 멀쩡한 척 열린다'
    },

    @{
        # 잘린 파일을 "손상됐다" 로 부른다. 다시 받으면 될 파일을 버리게 만든다.
        Name   = 'capture-file-cut-as-damage'
        File   = 'ProfileCaptureFile.cpp'
        Old    = "`t`tif (!all_chunks_within(*table, bytes.size()))`r`n`t`t{`r`n`t`t`treturn std::unexpected(capture_file_error::truncated);"
        New    = "`t`tif (!all_chunks_within(*table, bytes.size()))`r`n`t`t{`r`n`t`t`treturn std::unexpected(capture_file_error::checksum_mismatch);"
        Expect = 'file-reject/cut-is-truncated'
        Why    = '잘림과 손상을 가르지 않으면 사용자가 할 일을 잘못 고른다'
    },

    @{
        # 같은 폭의 두 필드를 바꿔 쓴다. 크기가 같으니 읽기는 성공하고,
        # 스레드와 깊이가 **조용히** 뒤바뀐다 — 수만 세는 검사는 못 잡는다.
        Name   = 'capture-file-swaps-fields'
        File   = 'ProfileCaptureFile.cpp'
        Old    = "`t`tout.put(value.thread_slot);`r`n`t`tout.put(value.depth);"
        New    = "`t`tout.put(value.depth);`r`n`t`tout.put(value.thread_slot);"
        Expect = 'file/round-trip/event-fields'
        Why    = '필드가 뒤바뀌어도 수는 같다 — 필드 전부를 봐야 잡힌다'
    },

    @{
        # 프레임 순서를 안 본다. find_frame 의 이분 탐색이 엉뚱한 프레임을 낸다.
        Name   = 'capture-file-accepts-unordered'
        File   = 'ProfileCaptureFile.cpp'
        Old    = "`t`t`tif (i > 0 && frames[i - 1].engine_frame >= frames[i].engine_frame)"
        New    = "`t`t`tif (i > 0 && frames[i - 1].engine_frame == frames[i].engine_frame)"
        Expect = 'file-reject/order'
        Why    = '순서가 어긋난 캡처는 멈추지 않고 엉뚱한 프레임을 낸다'
    },

    @{
        # 개수를 믿는다. CRC 가 맞는 거대한 개수 하나로 프레임 43 억 칸을
        # 잡으려 든다.
        Name   = 'capture-file-trusts-count'
        File   = 'ProfileCaptureFile.cpp'
        Old    = "`t`tif (!in.get(count) || !in.can_hold(count, kFrameHeadBytes))"
        New    = "`t`tif (!in.get(count) || !in.can_hold(0, kFrameHeadBytes))"
        Expect = 'file-reject/bounded-allocation'
        Why    = '개수를 믿으면 손상된 파일 하나가 할당으로 프로세스를 죽인다'
    },

    @{
        # 교체하지 않고 복사한다. 결과 파일은 맞지만 임시 파일이 남는다.
        Name   = 'capture-file-leaves-temp'
        File   = 'ProfileCaptureFile.cpp'
        Old    = "`t`tstd::filesystem::rename(temporary, path, error);"
        New    = "`t`tstd::filesystem::copy_file(temporary, path, std::filesystem::copy_options::overwrite_existing, error);"
        Expect = 'file-disk/no-temp'
        Why    = '교체 대신 복사하면 저장할 때마다 임시 파일이 쌓인다'
    },

    # ── P6-3 파일 열기 ─────────────────────────────────────────────
    @{
        # 열어도 따라가기를 안 끈다. 다음 프레임의 sync() 가 라이브로 덮어써
        # 연 파일이 한 프레임만 보이고 사라진다 — 화면에서는 "안 열렸다" 로 보인다.
        Name   = 'reader-open-keeps-following'
        File   = 'ProfileReader.cpp'
        Old    = "`t`t// 파일을 보는 동안은 라이브를 따라가지 않는다 — 다음 sync() 가 덮는다.`r`n`t`tm_liveFollow = false;"
        New    = "`t`t// 파일을 보는 동안은 라이브를 따라가지 않는다 — 다음 sync() 가 덮는다.`r`n`t`tm_liveFollow = true;"
        Expect = 'open/stops-following'
        Why    = '따라가기를 안 끄면 연 파일이 다음 프레임에 라이브로 덮인다'
    },

    @{
        # 라이브의 그래프 창을 이어 간다. 파일 범위의 앞머리에 잘려 붙어
        # 최신을 안 보인다.
        Name   = 'reader-open-keeps-graph'
        File   = 'ProfileReader.cpp'
        Old    = "`t`t// set_graph_span 이 파일의 최신 끝에 창을 세운다.`r`n`t`treset_graph();"
        New    = "`t`t// set_graph_span 이 파일의 최신 끝에 창을 세운다."
        Expect = 'open/graph-at-latest'
        Why    = '라이브의 그래프 창을 이어 가면 파일의 앞머리에 서서 최신을 안 보인다'
    },

    @{
        # 보던 선택을 이어 간다. 라이브의 1 번이 파일 범위의 가장자리(100)에
        # 잘려 붙는다.
        Name   = 'reader-open-keeps-selection'
        File   = 'ProfileReader.cpp'
        Old    = "`t`t// 파일 범위 가장자리에 잘려 붙는다.`r`n`t`tselect_latest();"
        New    = "`t`t// 파일 범위 가장자리에 잘려 붙는다."
        Expect = 'open/selects-latest'
        Why    = '보던 선택을 이어 가면 남의 번호가 파일 가장자리에 잘려 붙는다'
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
        # ★ P6 에서 freeze 가 시계를 함께 받게 되며 호출이 두 줄로 갈렸다.
        #   앵커는 둘째 줄의 **온전함 인자**만 잡는다 — 같은 줄을 시계 변이
        #   (capture-clock-dropped)도 잡지만, 변이는 각자 원본에 따로 걸리므로
        #   서로를 가리지 않는다.
        Old    = "`t`t`tthreads, capture_environment{ ticks_per_second() }, 0 == unacked, unacked);"
        New    = "`t`t`tthreads, capture_environment{ ticks_per_second() }, true, 0);"
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
    },

    # ── §7.3 의 트랙 순서와 GPU 귀속 ───────────────────────────────
    @{
        # 트랙을 안 보고 트랙 안의 순서부터 본다. 그러면 레인이 등록 순서로
        # 돌아가고, 워커의 등록 순서는 회차마다 갈린다.
        Name   = 'track-order-ignores-kind'
        File   = 'ProfileThreadStream.cpp'
        Old    = "`t`tif (a.kind != b.kind) return a.kind < b.kind;`r`n"
        New    = ""
        Expect = 'track-order/lane'
        Why    = '트랙을 안 보면 레인 순서가 등록 순서로 돌아간다'
    },
    @{
        # 레인의 스팬 경계는 요약이 **이벤트와 같은 순서**일 때 걸어 둔 것이다.
        # 그 전제를 깨면 아무것도 실패하지 않고 모든 레인이 빈다 — 순서를
        # 바꾸는 자리(2b)를 경계보다 앞으로 옮기면 정확히 이 모양이 된다.
        Name   = 'spans-walk-out-of-order'
        File   = 'ProfileAggregate.cpp'
        Old    = "`t`t{`r`n`t`t`tstd::uint32_t cursor = 0;"
        New    = "`t`tstd::reverse(result.m_threads.begin(), result.m_threads.end());`r`n`t`t{`r`n`t`t`tstd::uint32_t cursor = 0;"
        Expect = 'track-order/range'
        Why    = '요약 순서가 이벤트 순서와 어긋나면 레인 경계가 통째로 빈다'
    },
    @{
        # 제출 번호를 안 싣는다. GPU bar 의 tooltip 이 같은 프레임의 씬뷰와
        # 게임뷰 제출을 가릴 수 없게 된다.
        Name   = 'gpu-origin-dropped'
        File   = 'ProfileThreadStream.cpp'
        Old    = "`t`tvalue.submission = gpu.submission;"
        New    = "`t`tvalue.submission = 0;"
        Expect = 'gpu-origin/submission'
        Why    = '귀속을 안 실으면 같은 이름의 두 제출이 구분되지 않는다'
    },

    # ── §7.3 트랙 1: 프레임 경계와 길이 없는 사건 ──────────────────
    @{
        # 프레임 경계를 모으지 않는다. 그리는 층은 프레임이 어디서 갈리는지
        # 알 수 없고, 확대하면 "무언가 오래 걸린다" 까지만 읽힌다.
        Name   = 'boundaries-dropped'
        File   = 'ProfileAggregate.cpp'
        Old    = "`t`t`tresult.m_boundaries.push_back(boundary);"
        New    = ""
        Expect = 'boundary/count'
        Why    = '경계를 안 모으면 막대가 어느 프레임의 것인지 알 수 없다'
    },
    @{
        # 길이 없는 사건을 트리에 넣는다. total 0 · calls N 인 줄이 시간
        # 순위표에 섞인다.
        #
        # ★ 처음에는 Expect 를 'instant/keeps-parent'(사건 뒤에 열린 구간이
        #   부모를 지키는가)에 걸었는데, **그 절은 안 울렸다.** write_instant
        #   가 깊이를 0 이 아니라 **지금 열려 있는 깊이**로 적기 때문에, 트리에
        #   들어가도 부모를 밀어내지 못한다. 실제로 걸리는 자리로 옮겼다 —
        #   짐작한 인과를 그대로 두면 그 절에 이빨이 있다는 거짓말이 된다.
        Name   = 'instant-into-tree'
        File   = 'ProfileAggregate.cpp'
        Old    = "`t`t`tif (is_instant(event))`r`n`t`t`t{`r`n`t`t`t`tcontinue;`r`n`t`t`t}`r`n`r`n`t`t`t// 스레드가 바뀌면"
        New    = "`t`t`t// 스레드가 바뀌면"
        Expect = 'instant/not-in-tree'
        Why    = '점이 트리에 끼면 시간 순위표에 길이 없는 줄이 섞인다'
    },
    @{
        # 사건에 표식을 안 단다. 집계가 길이 0 짜리 스코프로 읽어 경계 띠가
        # 비고, 그러면서 트리에는 들어간다.
        Name   = 'instant-unflagged'
        File   = 'ProfileThreadStream.cpp'
        Old    = "`t`tvalue.flags = event_flags::instant;"
        New    = "`t`tvalue.flags = event_flags::none;"
        Expect = 'instant/present'
        Why    = '표식이 없으면 길이 0 인 스코프와 구분되지 않는다'
    },

    # ── §6.4 개정: 녹화 중 공개 ─────────────────────────────────────
    @{
        # 녹화 중에는 스냅샷을 내지 않는다. 그러면 "보려면 멈춰라" 로 되돌아간다.
        Name   = 'live-capture-never-published'
        File   = 'ProfileService.cpp'
        Old    = "`t`tpublish_live_capture(tick);"
        New    = ""
        Expect = 'live/published'
        Why    = '녹화 중에 공개하지 않으면 멈춰야만 프레임을 볼 수 있다'
    },
    @{
        # 청하지 않아도 낸다. 창이 닫혀 있어도 프레임마다 링을 통째로 복사하고,
        # 그 비용은 프로파일러가 스스로 만든 것이라 어느 마커에도 안 잡힌다.
        Name   = 'live-capture-ignores-request'
        File   = 'ProfileService.cpp'
        Old    = "`t`tif (!m_liveCaptureRequested.load(std::memory_order_relaxed))`r`n`t`t{`r`n`t`t`treturn;`r`n`t`t}"
        New    = ""
        Expect = 'live/unrequested'
        Why    = '청하지 않아도 내면 창이 닫혀 있는 동안에도 링을 복사한다'
    },
    @{
        # 공개하면서 녹화를 멈춘다. 지금 도구가 그러했다.
        Name   = 'live-capture-stops-recording'
        File   = 'ProfileService.cpp'
        Old    = "`t`t`tm_capture = std::move(live);`r`n`t`t}`r`n`t}"
        New    = "`t`t`tm_capture = std::move(live);`r`n`t`t}`r`n`t`tm_state.store(recorder_state::frozen, std::memory_order_release);`r`n`t}"
        Expect = 'live/still-recording'
        Why    = '공개가 녹화를 멈추면 Unity 의 Current Frame 모드가 서지 않는다'
    },

    # ── §7.2 프레임 그래프의 창 ──────────────────────────────────────
    @{
        # 폭을 정할 때 왼쪽 끝에 붙인다. 녹화 중에 최신 프레임이 화면 밖으로
        # 나가 버리고, 사람은 "녹화가 멈췄다" 로 읽는다.
        Name   = 'graph-span-anchors-left'
        File   = 'ProfileReader.cpp'
        Old    = "`t`tconst std::uint32_t anchor = graph_last();"
        New    = "`t`tconst std::uint32_t anchor = graph_first();"
        Expect = 'graph/span-anchors-right'
        Why    = '왼쪽에 붙이면 녹화 중 최신 프레임이 화면 밖에 있다'
    },
    @{
        # 새 캡처가 와도 창이 안 흐른다. 그래프가 옛 구간에 얼어붙는다.
        Name   = 'graph-does-not-flow'
        File   = 'ProfileReader.cpp'
        Old    = "`t`t`t`tm_graphFirst = (m_availableLast + 1 > span)`r`n`t`t`t`t`t? (m_availableLast + 1 - span) : m_availableFirst;"
        New    = "`t`t`t`tm_graphFirst = (m_availableLast + 1 > span)`r`n`t`t`t`t`t? m_graphFirst : m_availableFirst;"
        Expect = 'graph/flows'
        Why    = '창이 안 흐르면 녹화 중 새 프레임이 그래프에 안 들어온다'
    },
    @{
        # 뒤로 굴려도 따라가기가 안 꺼진다. 다음 스냅샷이 창을 최신으로
        # 되돌려서, 손으로 굴린 것이 한 프레임 만에 사라진다.
        Name   = 'graph-pan-keeps-following'
        File   = 'ProfileReader.cpp'
        Old    = "`t`tset_live_follow(m_graphFirst >= lastFirst);"
        New    = "`t`tset_live_follow(m_graphFirst <= lastFirst);"
        Expect = 'graph/pan-stops-following'
        Why    = '굴려도 따라가기가 켜져 있으면 옛 구간을 읽을 수 없다'
    },
    @{
        # 오른쪽 경계를 프레임 수만큼 물리지 않는다. 창이 보존 끝을 넘어가
        # 빈 칸을 그리고, 사람은 그 프레임들이 사라졌다고 읽는다.
        Name   = 'graph-clamp-right-off-by-window'
        File   = 'ProfileReader.cpp'
        Old    = "`t`tconst std::uint32_t lastFirst = m_availableLast - m_graphCount + 1;"
        New    = "`t`tconst std::uint32_t lastFirst = m_availableLast;"
        Expect = 'graph/clamp-right'
        Why    = '오른쪽 경계를 창 폭만큼 안 물리면 창이 보존 밖으로 나간다'
    },

    @{
        # 선택이 창과 반대로 움직인다. 스크롤 하나가 전부를 민다는 계약이
        # 깨지면 위 그래프와 아래 타임라인이 서로 다른 자리를 말한다.
        Name   = 'graph-pan-moves-selection-backwards'
        File   = 'ProfileReader.cpp'
        Old    = "`t`t`tstatic_cast<std::int64_t>(m_graphFirst) - static_cast<std::int64_t>(before);"
        New    = "`t`t`tstatic_cast<std::int64_t>(before) - static_cast<std::int64_t>(m_graphFirst);"
        Expect = 'graph/selection-follows-pan'
        Why    = '선택이 창을 안 따라가면 스크롤이 위쪽 그림만 흔든다'
    },

    # ── §7.4 Min·P95·Frames ─────────────────────────────────────────
    @{
        # p95 의 등수를 표본 수로 바꾼다. 긴 꼬리를 보려고 만든 열이 "가장
        # 나쁜 한 번" 이 되어, 늘 그만큼 걸린다고 읽힌다.
        #
        # ★ 처음에는 `row.p95_ticks` 를 직접 `samples[end - 1]` 로 바꿨는데
        #   **컴파일이 안 됐다.** 이 게이트는 /W4 /WX 라, 그 변이가 `rank` 를
        #   미사용으로 만들면 C4189 가 오류가 된다. 변이는 값만 틀리게 해야
        #   하고, 쓰이던 것을 안 쓰이게 만들면 안 된다.
        Name   = 'p95-reports-max'
        File   = 'ProfileAggregate.cpp'
        Old    = "`t`t`tconst std::size_t rank = (count * 95 + 99) / 100;"
        New    = "`t`t`tconst std::size_t rank = count;"
        Expect = 'distribution/p95'
        Why    = 'p95 가 max 와 같아지면 꼬리를 볼 수 없다'
    },
    @{
        # 표본을 내림차순으로 세운다. min 자리에 max 가 온다 — 두 열이
        # 서로를 베끼는데 둘 다 "값이 있다" 는 사실은 그대로다.
        Name   = 'distribution-sorted-descending'
        File   = 'ProfileAggregate.cpp'
        Old    = "`t`t`t          return a.ticks < b.ticks;"
        New    = "`t`t`t          return a.ticks > b.ticks;"
        Expect = 'distribution/min'
        Why    = '오름차순이 아니면 min·p95 가 모두 다른 자리를 가리킨다'
    },
    @{
        # 프레임 중복을 안 지운다. Frames 가 Calls 와 같아지고, 두 열이
        # 같은 것을 말하게 된다 — 그러면 Frames 열이 있을 까닭이 없다.
        Name   = 'frames-count-duplicates'
        File   = 'ProfileAggregate.cpp'
        Old    = "`t`t`tif (i > 0 && samples[i].row == samples[i - 1].row &&`r`n`t`t`t    samples[i].frame == samples[i - 1].frame)`r`n`t`t`t{`r`n`t`t`t`tcontinue;`r`n`t`t`t}`r`n"
        New    = ""
        Expect = 'distribution/frames'
        Why    = '중복을 안 지우면 나타난 프레임 수가 호출 수가 된다'
    },
    @{
        # Flat 이 갈래의 분포를 베낀 채 시작한다. 프레임 수는 `+=` 라 그 위에
        # 더해져, 한 프레임에 두 부모 밑으로 불린 marker 가 두 프레임으로
        # 잡힌다 — 캡처에 있는 프레임 수보다 커질 수도 있다.
        Name   = 'flat-copies-branch-distribution'
        File   = 'ProfileAggregate.cpp'
        Old    = "`t`t`t`t`tfresh.frame_appearances = 0;"
        New    = "`t`t`t`t`tfresh.frame_appearances = node.frame_appearances;"
        Expect = 'flat-union/frames'
        Why    = '부분의 프레임 수를 더하면 전체의 프레임 수가 안 된다'
    }
)

# ★ 고른 것이 하나도 없으면 붉다. 오타 난 이름이 "변이 0 개 통과" 로 읽히면
#   검사를 안 한 것이 통과로 남는다.
$mutationTotal = $mutations.Count
if ($Only.Count -gt 0) {
    $mutations = @($mutations | Where-Object {
        $name = $_.Name
        @($Only | Where-Object { $name -like $_ }).Count -gt 0
    })
    if ($mutations.Count -eq 0) {
        Write-Host ("[FAIL] -Only '" + ($Only -join "','") + "' 에 맞는 변이가 없다") -ForegroundColor Red
        exit 1
    }
}

$sources = @(
    (Join-Path $core 'ProfileMarker.cpp'),
    (Join-Path $core 'ProfileThreadStream.cpp'),
    (Join-Path $core 'ProfileCapture.cpp'),
    (Join-Path $core 'ProfileCaptureFile.cpp'),
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
if ($mutations.Count -lt $mutationTotal) {
    # 골라 돌린 판정은 전부 돈 판정과 다르게 적는다 — 나중에 읽는 사람이 헷갈리지 않게.
    Write-Host ("프로파일러 코어 통과 — 계약이 서고, 고른 변이 {0} / 전체 {1} 이 각각 제 검사에서 붉어진다" -f $mutations.Count, $mutationTotal)
}
else {
    Write-Host ("프로파일러 코어 통과 — 계약이 서고, 변이 {0} 이 각각 제 검사에서 붉어진다" -f $mutations.Count)
}

# 종료 코드를 명시한다. 판정이 종료 코드뿐인 집중 검사 방식에서는 성공
# 경로가 남의 $LASTEXITCODE 를 흘리면 게이트가 판정 능력을 잃는다
# (verify-editor-icon-resources.ps1 이 git grep 의 1 을 흘리고 있었다).
exit 0
