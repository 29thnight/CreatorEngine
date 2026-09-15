[CmdletBinding()]
param(
    [string]$Exe = (Join-Path $PSScriptRoot '../../Bin/x64-Release/Editor/CreatorEditor.exe'),
    [string]$Work = (Join-Path $env:TEMP 'creator-browser-thumbnail')
)
# PHASE 21 W7 — Content Browser 썸네일 계약.
#
# ── 판정문에 자가 없었다 ───────────────────────────────────────────────────
#
# 계약은 "썸네일은 **실제 GPU 사용 가능 시점**을 따른다" 고 적었다. 그런데 그
# 값을 물을 수단이 없었다. `RegisterTexture` 는 업로드 전에도 0 이 아닌 id 를
# 돌려준다 — DX12 는 프레임이 열려 있지 않으면 서술자 칸만 예약하고 널 SRV 를
# 써 둔다. 반환값을 "됐다" 로 읽으면 한 프레임이 빈 그림으로 나가고, 그 한
# 프레임은 어떤 계수기에도 남지 않는다.
#
# 그래서 W7 은 먼저 자를 세웠다(`IsTextureReady`). 이 게이트는 그 자를 포함해
# 계약의 각 절이 **자극된 채로** 지켜지는지를 본다.
#
# ── 자극이 이 게이트의 절반이다 ────────────────────────────────────────────
#
# 브라우저 뿌리(`Dynamic_CPP/Assets`)에는 이미지가 **0** 이다. 그래서 기본
# 배치로 한 바퀴 돌리면 썸네일은 한 번도 요청되지 않고, 장부는 전부 0 이며
# 모든 단정이 통과한다. 그 초록은 "지킨다" 가 아니라 "묻지 않았다" 다.
#
# 추적되는 fixture 셋을 뿌리에 올려 자극한다(`Tools/regression/fixtures/
# browser-thumbnails`). `Dynamic_CPP/Assets` 는 대부분 ignore 라 거기 기대면
# 게이트가 조용히 빈다 — fixture 는 추적 대상에 두고 회차마다 올리고 지운다.
#
#   Checker256.png  256x256 -> 축소 경로(>128).  128*128*4 = 65,536 B
#   Tiny4.png         4x4   -> 통과 경로(<=128).       4*4*4 =     64 B
#   Undecodable.png 서명만 진짜 -> 디코딩 실패 경로.           0 B
#
# ★ 첫 실패 fixture 는 IDAT 만 망가뜨렸는데 WIC 가 삼켜 8x8 을 냈다. 실패
#   경로는 **자극되지 않았고**, 그것이 드러난 것은 `bytes` 가 산술보다 256
#   컸기 때문이다. 그래서 이 게이트는 바이트를 **등호로** 잰다 — 부등호로
#   재면 그 유령이 다시 지나간다.
#
# ── 한 회차 안에서 세 단을 만든다 ──────────────────────────────────────────
#
# 무효화·축출은 프로세스를 다시 띄우면 자극할 수 없다(캐시가 프로세스 안에
# 있다). 그래서 한 회차 안에서 단을 나누고, 에디터가 **도는 중에** 원본을 만진다.
# 동기점은 장부 A 직후의 `scene.save` 가 만드는 표지 파일이다 — 결과 JSONL 은
# 줄마다 flush 되지만 에디터가 독점으로 열고 있어 밖에서 읽을 수 없다.
#
#   장부 A  첫 자극 — 요청·디코딩·게시·실패·중복 억제
#   장부 B  A 뒤에 원본을 다시 쓴다 -> 무효화·재요청
#   장부 C  예산을 낮춘다 -> 축출
#
# 축출에는 창구가 필요했다. 목록은 clipper 로 **보이는 타일만** 요청하므로 파일을
# 수백 개 뿌려도 기본 예산 48 MB 에 닿지 않는다. 예산이 `constexpr` 인 동안 그
# 절은 영원히 미자극이었다 — `editor.thumbnail budget <bytes>` 가 그 자를 연다.
#
# ── 소스 축이 따로 필요한 이유 ─────────────────────────────────────────────
#
# 런타임이 못 보는 것이 셋 있다.
#
#   ① **어느 스레드에서 디코딩하는가.** 메인 스레드에서 풀어도 그림은 맞게 나온다.
#      장부도 같다. 달라지는 것은 프레임이 끊긴다는 것뿐이고, 시간은 기계에 묶여
#      판정으로 쓸 수 없다.
#   ② **예산 창구가 실물인가.** 비교가 상수를 직접 읽으면 `budget` 명령은 아무
#      일도 하지 않고, 장부 C 는 `evicted 0` 을 낸다. 그때 이 게이트가 붉어지긴
#      하지만 "자극하지 못했다" 와 "지키지 않는다" 를 못 가른다.
#   ③ **Vulkan 팔이 배선돼 있는가.** 이 회차는 DX12 다. PHASE 21 은 DX12 판정이고
#      Vulkan 은 이 페이즈의 대상이 아니지만, **미루는 것은 판정이지 배선이
#      아니다** — 자를 한 팔에만 달면 다음 페이즈가 없는 자를 물려받는다.

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$repo = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$Exe = [IO.Path]::GetFullPath($Exe)
$Work = [IO.Path]::GetFullPath($Work)

if (-not (Test-Path -LiteralPath $Exe)) { throw "Editor executable not found: $Exe" }
if ($Exe -notmatch 'x64-Release') {
    throw "성능 판정은 Release 로만 한다 — Debug 는 25 배 느리고 방향까지 뒤집는다. 받은 경로: $Exe"
}
if (Get-Process CreatorEditor -ErrorAction SilentlyContinue) {
    throw '이미 떠 있는 에디터를 닫아라 — endpoint 파일은 프로젝트에 하나다.'
}
if (Test-Path -LiteralPath $Work) { Remove-Item -LiteralPath $Work -Recurse -Force }
New-Item -ItemType Directory -Force -Path $Work | Out-Null

$script:checks = 0
function Assert([bool]$Condition, [string]$Message) {
    if (-not $Condition) { throw $Message }
    $script:checks++
}

# ★ 결과 JSONL 은 동기점으로 쓸 수 없다 — 에디터가 **독점으로** 열고 있어서
#   읽으려 들면 sharing violation 이 난다(줄마다 flush 하는 것과 별개다).
#   그래서 표지는 에디터가 스스로 만드는 **다른 파일**로 잡는다: 장부 A 직후에
#   `scene.save` 를 한 번 시키고, 그 파일이 생기는 것을 기다린다.

Write-Host ''
Write-Host '브라우저 썸네일 계약 (PHASE 21 W7)'
Write-Host ''

# ══ fixture 를 뿌리에 올린다 ═══════════════════════════════════════════════

$assets   = Join-Path $repo 'Dynamic_CPP/Assets'
$fixtures = Join-Path $PSScriptRoot 'fixtures/browser-thumbnails'
$names    = @('Checker256.png', 'Tiny4.png', 'Undecodable.png')
$stimulus = 'Checker256.png'   # 무효화 단에서 다시 쓸 원본
$placed   = @()

Assert (Test-Path -LiteralPath $assets) "브라우저 뿌리가 없다: $assets"
foreach ($n in $names) {
    Assert (Test-Path -LiteralPath (Join-Path $fixtures $n)) "fixture 가 없다: $n"
}

try {
    foreach ($n in $names) {
        $destination = Join-Path $assets $n
        if (Test-Path -LiteralPath $destination) {
            throw "뿌리에 이미 $n 이 있다 — 지난 회차가 안 지워졌거나 실제 자산과 이름이 겹친다."
        }
        # ★ Copy-Item 은 원본의 mtime 을 **보존한다.** 여기서는 그래도 되지만
        #   무효화 단에서는 그것이 자극을 통째로 없앤다(아래 참조).
        Copy-Item -LiteralPath (Join-Path $fixtures $n) -Destination $destination
        $placed += $destination
    }
    Write-Host ('  fixture {0} 개를 뿌리에 올렸다: {1}' -f $names.Count, ($names -join ' / '))

    # ══ 회차 ═══════════════════════════════════════════════════════════════

    $ws = Join-Path $Work 'ws'
    New-Item -ItemType Directory -Force -Path $ws | Out-Null
    $scriptPath = Join-Path $Work 'run.txt'
    $resultPath = Join-Path $Work 'run.jsonl'
    $markPath = Join-Path $Work 'ledger-a.mark'

    $lines = [Collections.Generic.List[string]]::new()
    # ★ 썸네일 장부는 **워밍업 앞**에서 비운다. 뒤에 두면 요청·디코딩·게시가 전부
    #   부팅 구간에서 일어나 버려지고 requests 가 0 으로 보인다. 구간을 가르는
    #   장치는 가르는 순간 앞쪽을 버린다.
    $lines.Add('editor.thumbnail reset')            # 행 #0
    $lines.Add('wait 240')                          # 워밍업 — 도크 구축·최초 스캔
    $lines.Add('editor.panelcost reset')            # 비용 축만 부팅 구간을 뺀다
    foreach ($i in 1..20) { $lines.Add('editor.dock'); $lines.Add('wait 20') }
    $lines.Add('editor.thumbnail')                  # 행 #1 = 장부 A
    $lines.Add('scene.save ' + $markPath)           # ★ 동기점 — 이 파일이 생기면 A 가 찍혔다
    $lines.Add('wait 900')                          # 이 창에서 원본을 다시 쓴다
    foreach ($i in 1..20) { $lines.Add('editor.dock'); $lines.Add('wait 20') }
    $lines.Add('editor.thumbnail')                  # 행 #2 = 장부 B
    $lines.Add('editor.thumbnail budget 40000')     # 행 #3
    foreach ($i in 1..15) { $lines.Add('editor.dock'); $lines.Add('wait 20') }
    $lines.Add('editor.thumbnail')                  # 행 #4 = 장부 C
    $lines.Add('editor.thumbnail budget default')   # 행 #5
    $lines.Add('editor.panelcost')
    $lines.Add('quit')
    Set-Content -LiteralPath $scriptPath -Encoding UTF8 -Value $lines.ToArray()

    $priorWorkspace = $env:CREATOR_EDITOR_WORKSPACE_DIR
    $priorLegacy = $env:CREATOR_EDITOR_LEGACY_INI
    # ini 를 지운 선언 배치. 개발자의 저장 배치는 Content Browser 가 뒤 탭일 수
    # 있고 그러면 타일이 **그려지지 않아** 장부가 통째로 0 이 된다 — 눈먼 초록이다.
    $env:CREATOR_EDITOR_WORKSPACE_DIR = $ws
    $env:CREATOR_EDITOR_LEGACY_INI = Join-Path $ws 'none.ini'

    $stimulusPath = Join-Path $assets $stimulus
    $revisionBefore = (Get-Item -LiteralPath $stimulusPath).LastWriteTimeUtc
    $stimulated = $false

    try {
        $proc = Start-Process -FilePath $Exe -WorkingDirectory (Split-Path $Exe) -WindowStyle Hidden -PassThru `
            -ArgumentList @('--script', ('"' + $scriptPath + '"'), '--result-format', 'jsonl',
                            '--result-file', ('"' + $resultPath + '"')) `
            -RedirectStandardOutput (Join-Path $Work 'run.out') `
            -RedirectStandardError (Join-Path $Work 'run.err')

        # 표지가 생기기를 기다린다. 부팅이 16 초쯤 걸리므로 넉넉히 준다.
        $deadline = [DateTime]::UtcNow.AddSeconds(240)
        while ([DateTime]::UtcNow -lt $deadline) {
            if ($proc.HasExited) { break }
            if (Test-Path -LiteralPath $markPath) { break }
            Start-Sleep -Milliseconds 120
        }

        if ((-not $proc.HasExited) -and (Test-Path -LiteralPath $markPath)) {
            # ── 무효화 자극 ────────────────────────────────────────────────
            #
            # 같은 바이트를 다시 쓴다. 키는 경로 해시 + revision 이고 revision 은
            # `last_write_time ^ file_size` 이므로, 내용이 같아도 mtime 이 달라지면
            # 키가 갈린다. 목록 캐시는 나이(`kRevalidateMs` 1 초)로 다시 훑으므로
            # 감시자 없이도 다음 스캔이 새 revision 을 집어 온다.
            #
            # ★ `Copy-Item` 으로 덮으면 mtime 까지 원본 값으로 되돌아가 revision 이
            #   그대로다 — 자극이 통째로 사라지고 장부 B 는 `invalidated 0` 을
            #   낸다. 그래서 바이트를 직접 쓰고 mtime 을 명시로 밀어 둔다.
            [IO.File]::WriteAllBytes($stimulusPath,
                [IO.File]::ReadAllBytes((Join-Path $fixtures $stimulus)))
            $bumped = $revisionBefore.AddSeconds(5)
            if ($bumped -lt [DateTime]::UtcNow) { $bumped = [DateTime]::UtcNow }
            (Get-Item -LiteralPath $stimulusPath).LastWriteTimeUtc = $bumped
            $stimulated = $true
        }

        if (-not $proc.WaitForExit(600000)) { $proc.Kill(); throw '에디터가 600 초 안에 끝나지 않았다.' }
    }
    finally {
        if ($null -ne $priorWorkspace) { $env:CREATOR_EDITOR_WORKSPACE_DIR = $priorWorkspace }
        else { Remove-Item Env:CREATOR_EDITOR_WORKSPACE_DIR -ErrorAction SilentlyContinue }
        if ($null -ne $priorLegacy) { $env:CREATOR_EDITOR_LEGACY_INI = $priorLegacy }
        else { Remove-Item Env:CREATOR_EDITOR_LEGACY_INI -ErrorAction SilentlyContinue }
    }

    Assert (Test-Path -LiteralPath $resultPath) "결과 파일이 없다 (종료 코드 $($proc.ExitCode)) — run.err 를 보라"
    $exitMessage = '회차 종료 코드가 비정상이다 (0x{0:X8}) — 32 면 다른 세션이 runtime.dll 을 쥐고 있다' -f $proc.ExitCode
    Assert (0 -eq $proc.ExitCode) $exitMessage

    $rows = @(Get-Content -LiteralPath $resultPath | Where-Object { $_.Trim() } |
              ForEach-Object { $_ | ConvertFrom-Json })
    $failedRows = @($rows | Where-Object { 'succeeded' -ne $_.status })
    $failedMessage = '실패한 명령 ' + $failedRows.Count + ' 건: ' +
        (@($failedRows | ForEach-Object { $_.command + '(' + $_.status + ') ' + $_.message }) -join ' · ')
    Assert (0 -eq $failedRows.Count) $failedMessage

    # 자극이 실제로 일어났는가. 이것을 단정하지 않으면 무효화 절이 미자극인 채
    # 초록으로 지나간다.
    Assert $stimulated '표지 파일이 제 시각에 오지 않아 원본을 만질 창이 없었다 — 무효화를 자극하지 못했다'
    $revisionAfter = (Get-Item -LiteralPath $stimulusPath).LastWriteTimeUtc
    Assert ($revisionAfter -ne $revisionBefore) `
        "원본의 mtime 이 그대로다($revisionBefore) — revision 이 갈리지 않아 무효화를 자극하지 못했다"

    $ledgerRows = @($rows | Where-Object { 'editor.thumbnail' -eq $_.command })
    Assert (6 -eq $ledgerRows.Count) `
        ("editor.thumbnail 행이 " + $ledgerRows.Count + " 개다 — 6 이어야 한다(reset·A·B·budget·C·restore)")

    $a = $ledgerRows[1].data
    $b = $ledgerRows[2].data
    $c = $ledgerRows[4].data

    function Show-Ledger([string]$Tag, $Data) {
        Write-Host ('  {0}  requests {1}  deduped {2}  decoded {3}  published {4}  failed {5}' -f `
            $Tag, $Data.requests, $Data.deduped, $Data.decoded, $Data.published, $Data.failed)
        Write-Host ('      invalidated {0}  lateDropped {1}  evicted {2}  entries {3}  ready {4}  bytes {5} / 예산 {6}' -f `
            $Data.invalidated, $Data.lateDropped, $Data.evicted, $Data.entries,
            $Data.ready, $Data.bytes, $Data.budgetBytes)
        Write-Host ('      servedThumbnails {0}  servedIcons {1}  queued {2}  working {3}  awaitingUpload {4}' -f `
            $Data.servedThumbnails, $Data.servedIcons, $Data.queued, $Data.working, $Data.awaitingUpload)
    }
    Write-Host ''
    Show-Ledger '장부 A' $a
    Show-Ledger '장부 B' $b
    Show-Ledger '장부 C' $c
    Write-Host ''

    # ══ 장부 A — 자극·중복 억제·준비 ═══════════════════════════════════════

    # ① 자극 확인이 먼저다. 요청이 0 이면 아래 0 들은 전부 계약 준수로 읽힌다.
    Assert (3 -eq $a.requests) `
        ("요청이 " + $a.requests + " 건이다 — fixture 셋이 각각 한 번씩 요청돼 3 이어야 한다. " +
         "0 이면 타일이 그려지지 않았거나 뿌리가 비었다(자극하지 못했다).")

    # ② 중복 억제. 같은 원본을 수백 프레임 물었는데 요청은 셋뿐이어야 한다.
    Assert ($a.deduped -ge 100) `
        ("되묻기가 " + $a.deduped + " 건뿐이다 — 타일이 프레임마다 물어야 이 축이 선다(자극하지 못했다)")

    # ③ 성공·실패 두 경로가 **모두** 돌았다.
    Assert (2 -eq $a.decoded) ("디코딩 성공이 " + $a.decoded + " 건이다 — 2 여야 한다")
    Assert (2 -eq $a.published) ("게시가 " + $a.published + " 건이다 — 2 여야 한다")
    Assert (1 -eq $a.failed) `
        ("디코딩 실패가 " + $a.failed + " 건이다 — Undecodable.png 하나가 실패해야 한다. " +
         "0 이면 디코더가 쓰레기를 삼켜 그림을 만들어 낸 것이다(첫 fixture 가 그랬다).")

    # ④ 바이트 산술. **등호로** 잰다 — 부등호는 유령 디코딩을 통과시킨다.
    $expectedBytes = 128 * 128 * 4 + 4 * 4 * 4
    Assert ($expectedBytes -eq $a.bytes) `
        ("픽셀 " + $a.bytes + " B 를 들고 있다 — 128*128*4 + 4*4*4 = " + $expectedBytes +
         " 이어야 한다. 크면 실패해야 할 원본이 디코딩된 것이고, 작으면 축소가 덜 됐거나 통과 경로가 죽었다.")

    # ⑤ 준비 상태로 착지했다. 여기가 `IsTextureReady` 축이다 — Prime/IsReady 고리가
    #    끊기면 타일이 영원히 awaitingUpload 에 머물고 그림은 나오지 않는다.
    Assert (2 -eq $a.ready) `
        ("준비된 항목이 " + $a.ready + " 개다 — 2 여야 한다. awaitingUpload " + $a.awaitingUpload +
         " 가 남아 있으면 등록만 하고 업로드를 못 본 것이다(Prime/IsReady 고리).")
    Assert (0 -eq $a.queued -and 0 -eq $a.working) `
        ("아직 queued " + $a.queued + " · working " + $a.working + " 가 남았다 — 표본 구간이 짧다")
    Assert (3 -eq $a.entries) `
        ("표에 " + $a.entries + " 개가 있다 — 3 이어야 한다. 실패분도 남아야 재요청되지 않는다.")

    # ⑥ 타일이 실제로 썸네일을 그렸고, 준비 전에는 유형 아이콘으로 버텼다.
    Assert ($a.servedThumbnails -ge 1) `
        ("타일이 썸네일을 " + $a.servedThumbnails + " 번 그렸다 — 한 번도 안 그렸으면 그림 경로가 죽었다")
    Assert ($a.servedIcons -ge 1) `
        ("유형 아이콘 대체가 " + $a.servedIcons + " 번뿐이다 — 준비 전 대체 경로가 자극되지 않았다")

    # ⑦ 디코딩이 워커 풀에서 돈다. 풀이 죽어 있으면 이 회차는 다른 것을 잰 것이다.
    Assert ([bool]$a.workerPoolRunning) '워커 풀이 돌지 않았다 — 이 회차의 디코딩 축은 뜻이 없다'

    # ══ 장부 B — 무효화와 재요청 ═══════════════════════════════════════════

    Assert ($b.invalidated -ge 1) `
        ("원본을 다시 썼는데 무효화가 " + $b.invalidated + " 건이다 — 목록 스캔이 새 revision 을 " +
         "집어 오면 옛 항목이 버려져야 한다")
    Assert ($b.requests -gt $a.requests) `
        ("무효화 뒤 요청이 늘지 않았다(" + $a.requests + " -> " + $b.requests + ") — 버리고 다시 묻지 않았다")
    Assert ($b.decoded -gt $a.decoded) `
        ("무효화 뒤 디코딩이 늘지 않았다(" + $a.decoded + " -> " + $b.decoded + ") — 옛 픽셀을 그대로 쓰고 있다")
    # 내용이 같으므로 바이트는 되돌아와야 한다. 옛 항목의 바이트를 빼지 않으면 두 배가 된다.
    Assert ($expectedBytes -eq $b.bytes) `
        ("무효화 뒤 픽셀이 " + $b.bytes + " B 다 — " + $expectedBytes +
         " 이어야 한다. 크면 버릴 때 바이트를 빼지 않은 것이다(예산이 새어 나간다).")
    Assert (3 -eq $b.entries) ("무효화 뒤 표에 " + $b.entries + " 개가 있다 — 3 이어야 한다")

    # ══ 장부 C — 예산과 축출 ═══════════════════════════════════════════════

    # ★ 큰따옴표 안의 백틱은 escape 다 — "`budget" 은 백스페이스 + udget 으로 나간다.
    #   M5 회차의 실패 문장이 실제로 그렇게 깨져 나왔고, 그것을 고치려고 여기에 주석을
    #   끼웠더니 이번에는 **줄잇기가 끊겨** 메시지 인자가 통째로 사라졌다(단정은 남고
    #   문장만 빈다 — 대조 회차에서 문자열이 그냥 출력돼 드러났다). 주석은 위에 둔다.
    Assert (40000 -eq $c.budgetBytes) `
        ('예산이 ' + $c.budgetBytes + ' 다 — budget 40000 이 먹히지 않았다. 비교가 상수를 ' +
         '직접 읽고 있으면 창구는 장식이다(소스 축 S3 를 보라).')
    Assert ($c.evicted -ge 1) `
        ("예산을 " + $c.bytes + " B 아래로 낮췄는데 축출이 " + $c.evicted + " 건이다 — " +
         "오래 안 쓴 것부터 버려야 한다")
    Assert ($c.bytes -le $expectedBytes) `
        ("축출 뒤에도 픽셀이 " + $c.bytes + " B 다 — 버렸는데 바이트가 줄지 않았다")

    Write-Host ('  늦은 완료 폐기(lateDropped) = {0} — 판정하지 않는다. 진행 중인 작업이 있는 순간에' -f $b.lateDropped)
    Write-Host '  무효화가 닿아야 서는 축이라 프레임 단위로 확률적이다.'

    # ══ W7-5 계약이 썸네일을 켠 상태에서도 서 있는가 ═══════════════════════
    #
    # 썸네일은 목록이 담아 온 revision 을 쓴다. 타일마다 `last_write_time` 을 다시
    # 물으면 W7-5 가 방금 닫은 그 병이 돌아온다 — probes 가 그것을 잡는다.

    $costRows = @($rows | Where-Object { 'editor.panelcost' -eq $_.command })
    Assert ($costRows.Count -ge 1) 'editor.panelcost 가 돌지 않았다'
    $panels = $costRows[-1].data.panels
    foreach ($slot in 'browser_tree', 'browser_files') {
        $found = @($panels | Where-Object { $slot -eq $_.slot })
        Assert (1 -eq $found.Count) "$slot 슬롯이 비용 표에 없다"
        $panel = $found[0]
        Assert ($panel.frames -ge 5) ("$slot 이 그려진 프레임이 " + $panel.frames + " 뿐이다 — 자극하지 못했다")
        Assert (0 -eq $panel.totalProbes) `
            ("$slot 이 스캔 밖에서 디스크를 " + $panel.totalProbes + " 회 만졌다(probes) — " +
             "썸네일이 타일마다 원본을 묻고 있다면 W7-5 가 되돌아온 것이다")
        Write-Host ('  {0,-14} frames {1,4}  units {2,3}  avg {3,7:N3} ms  p95 {4,7:N3} ms  scans {5}  probes {6}' -f `
            $panel.slot, $panel.frames, $panel.lastUnits, $panel.avgMs, $panel.p95Ms,
            $panel.totalScans, $panel.totalProbes)
    }

    # ══ 소스 축 ════════════════════════════════════════════════════════════

    Write-Host ''
    $cachePath = Join-Path $repo 'Editor/EngineGUIWindow/BrowserThumbnailCache.cpp'
    Assert (Test-Path -LiteralPath $cachePath) 'BrowserThumbnailCache.cpp 가 없다'
    $cacheLines = [IO.File]::ReadAllLines($cachePath)
    $cacheCode = @($cacheLines | Where-Object {
        $trimmed = $_.TrimStart()
        -not ($trimmed.StartsWith('//') -or $trimmed.StartsWith('*') -or $trimmed.StartsWith('/*'))
    })

    # S1 — `IsTextureReady` 가 인터페이스 둘과 셸 둘, 그리고 호스트에 모두 있다.
    #      런타임 회차는 DX12 뿐이라 Vulkan 팔은 소스로만 지킬 수 있다.
    $readyWiring = @{
        'Editor/HostImGuiPresentation/RHI/IImGuiRendererBackend.h' = 1
        'Editor/HostImGuiPresentation/RHI/IImGuiHost.h'            = 1
        'Editor/HostImGuiPresentation/RHI/ImGuiHost.cpp'           = 1
        'Editor/HostImGuiPresentation/RHI/DX12/ImGuiDx12Shell.h'   = 1
        'Editor/HostImGuiPresentation/RHI/DX12/ImGuiDx12Shell.cpp' = 1
        'Editor/HostImGuiPresentation/RHI/Vulkan/ImGuiVulkanShell.h'   = 1
        'Editor/HostImGuiPresentation/RHI/Vulkan/ImGuiVulkanShell.cpp' = 1
    }
    foreach ($relative in $readyWiring.Keys) {
        $full = Join-Path $repo $relative
        Assert (Test-Path -LiteralPath $full) "$relative 가 없다"
        $hits = @([IO.File]::ReadAllLines($full) | Where-Object {
            $trimmed = $_.TrimStart()
            (-not ($trimmed.StartsWith('//') -or $trimmed.StartsWith('*'))) -and
            # ★ 경계를 세운다. `IsTextureReady` 만 찾으면 `IsTextureReadyRetired`
            #   같은 이름이 배선으로 세어진다 — 변이 M6 이 정확히 그것으로
            #   이 단정을 통과했다.
            ($_ -match 'IsTextureReady(?![A-Za-z0-9_])')
        })
        Assert ($hits.Count -ge $readyWiring[$relative]) `
            ("$relative 에 IsTextureReady 가 없다 — 판정은 DX12 지만 **배선은 미루지 않는다**. " +
             "한 팔에만 자를 달면 다음 페이즈가 없는 자를 물려받는다.")
    }

    # S2 — 디코딩은 작업 스레드 몫이다. 호출이 하나뿐이고 그 자리가 풀 안이어야 한다.
    #      메인 스레드에서 풀어도 장부는 같다 — 런타임이 못 보는 축이다.
    $generatorCalls = @($cacheCode | Where-Object {
        ($_ -match 'thumbnail_run_generator\s*\(') -and ($_ -notmatch 'thumbnail_completion\s+thumbnail_run_generator')
    })
    Assert (1 -eq $generatorCalls.Count) `
        ("thumbnail_run_generator 호출이 " + $generatorCalls.Count + " 자리다 — 하나여야 한다. " +
         "둘째 자리가 생기면 그 자리는 Presentation 스레드일 수 있고, 그래도 그림과 장부는 옳게 나온다.")
    $dispatchBody = ($cacheCode -join "`n")
    Assert ($dispatchBody -match 'WorkerPools[\s\S]{0,400}thumbnail_run_generator') `
        'thumbnail_run_generator 가 워커 풀 안에서 불리지 않는다 — 디코딩이 프레임을 멈춘다'

    # S3 — 예산 비교가 상수를 직접 읽지 않는다. 읽으면 `budget` 명령은 장식이고
    #      축출 절은 영원히 미자극이다(그리고 그 미자극은 초록으로 보인다).
    $constantCompare = @($cacheCode | Where-Object { $_ -match 'bytes\s*[<>]=?\s*kThumbnailBudgetBytes' })
    Assert (0 -eq $constantCompare.Count) `
        ("예산 비교가 상수를 직접 읽는 자리가 " + $constantCompare.Count + " 곳 있다 — " +
         "조종 창구를 두고 비교는 상수를 보면 축출 축을 자극할 수 없다")
    Assert ($dispatchBody -match 'budgetBytes\.load\s*\(') `
        '예산을 원자 값에서 읽는 자리가 없다 — 창구가 실물인지 확인할 수 없다'

    Write-Host ('  소스 축: IsTextureReady 배선 {0} 자리 · 디코딩 호출 1 자리(풀 안) · 예산 비교는 창구 경유' -f `
        $readyWiring.Count)
}
finally {
    foreach ($path in $placed) { Remove-Item -LiteralPath $path -Force -ErrorAction SilentlyContinue }
    # .meta 사이드카가 생겼을 수 있다 — 감시자가 자산을 등록하면서 만든다.
    foreach ($n in $names) {
        Remove-Item -LiteralPath (Join-Path $assets ($n + '.meta')) -Force -ErrorAction SilentlyContinue
    }
}

Write-Host ''
Write-Host ("BROWSER_THUMBNAIL_CONTRACT_OK ({0} checks)" -f $script:checks)
