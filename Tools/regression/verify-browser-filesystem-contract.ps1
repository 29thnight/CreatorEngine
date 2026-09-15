[CmdletBinding()]
param(
    [string]$Exe = (Join-Path $PSScriptRoot '../../Bin/x64-Release/Editor/CreatorEditor.exe'),
    [string]$Work = (Join-Path $env:TEMP 'creator-browser-fs-contract')
)
# PHASE 21 W7-5 — Content Browser 는 idle 프레임에 디스크를 만지지 않는다.
#
# ── 세는 자가 틀린 단위를 세고 있었다 ──────────────────────────────────────
#
# W7-1 이 매 프레임 디렉터리 스캔을 24 → 0 으로 없앴다. 그 게이트는 `scans` 를
# 보고 초록이었다. 그런데 `browser_tree` 는 그 뒤에도 **avg 1.375 ms · p95 1.829 ms
# · max 10.40 ms** 였다(2026-09-15, PHASE 14 임시 계측이 게임 스레드 쪽에서 잡았다).
# 스캔은 0 인데 비쌌다.
#
# 빠져나간 것은 `std::filesystem::equivalent` 다. `ShowDirectoryTree` 가 노드마다
# *"이 폴더가 프리팹 폴더인가"* 를 **디스크에 물었다** — Windows 에서 그 함수는 두
# 경로를 실제로 열어(`CreateFile` + `GetFileInformationByHandle`) 파일 식별자를
# 비교한다. 노드 24 × 경로 2 = 프레임당 파일 핸들 48 회. 목록 캐시를 경유할 수
# 없는 종류의 호출이라 `scans` 계수기 **옆으로** 지나갔다.
#
# ★ 계약은 *"브라우저가 프레임마다 디스크를 만지지 않는다"* 인데 계수기는
#   *"디렉터리를 훑은 횟수"* 를 셌다. 강제 단위가 계약의 단위와 달랐다. 그래서
#   `probes`(스캔이 아닌 디스크 접촉)를 새 축으로 열고 **둘 다 0** 을 판정으로 둔다.
#
# ── 왜 런타임만으로는 모자란가 ─────────────────────────────────────────────
#
# 고침의 핵심은 **순서**다. 비교는 드래그 중에만 뜻이 있는데 `&&` 왼쪽에 있어
# 평상시에도 돌았다. `ImGui::BeginDragDropTarget()` 은 드래그가 없으면 즉시 거짓을
# 돌려주므로 관문을 앞으로 옮기면 평상시 비용이 0 이 된다.
#
# 그런데 **CLI 에는 드래그를 만들 창구가 없다.** 순서를 되돌려도 idle 런타임 축은
# probes 로 잡지만, "드래그 중에는 여전히 도는가" 는 못 본다. 그리고 누군가
# `equivalent` 를 다른 자리에 새로 심으면 그 자리가 매 프레임인지 아닌지를 런타임
# 표본이 우연히 비껴갈 수 있다. 그래서 **소스 축을 함께 세운다** — 양쪽을 다
# 소스에서 유도해 맞댄다.
#
# ── 판정 ───────────────────────────────────────────────────────────────────
#
#   런타임 ① idle 회차에서 browser_tree · browser_files 의 probes 가 **0**
#          ② 같은 회차에서 scans 가 예산 안(W7-1 계약이 살아 있다)
#          ③ ContentBrowser 창 비용과 browser_tree p95 를 기록한다(판정 아님 —
#             시간은 기계에 묶인다)
#   소스   ④ 브라우저 코드에 `equivalent` 가 **하나도 없다**. 면제 하나를 이유와
#             함께 두고 **면제 자체도 단정한다** — 그 자리가 사라졌는데 목록이
#             남으면 다음에 같은 이름의 새 호출이 조용히 그 자리를 물려받는다
#          ⑤ 디스크를 만지는 통로 둘 각각에서 계수가 호출 **앞**에 있다 —
#             결과가 아니라 호출에 건다
#          ⑥ 드래그 관문(`BeginDragDropTarget`)이 경로 비교보다 **앞**에 온다
#          ⑦ 그 통로 둘이 **실제로 불린다** — 소비자 0 인 통로를 지키는 계수기는
#             빈 단정이다
#          ⑧ 슬롯 배선(`add_panel_probes`)이 두 자리에 살아 있다
#          ⑨ 전역 계수 구문 수가 통로 수와 같다
#
# ⑧⑨ 이 없으면 **계측을 죽이는 변이가 초록으로 지나간다** — 배선을 지우면
# 슬롯 probes 는 영원히 0 이고 ①은 그것을 계약 준수로 읽는다. 슬롯 축을 0 이
# 아닌 값으로 만드는 자극은 드래그뿐이라 CLI 로 만들 수 없다. 계수기가 실제로
# **센다**는 것은 변이 회차가 증명한다 — 옛 코드를 복원하면 probes 가 노드 수만큼
# 나온다. 그 회차가 이 축의 양성 대조군이다.
#
# ── 두 축이 잡는 것이 다르다 (변이로 확인) ────────────────────────────────
#
# 옛 코드를 통째로 복원하는 변이(비교를 디스크로 · 관문을 뒤로)를 태워 봤더니
# **런타임 축이 못 잡았다** — browser_tree 는 avg 1.398 ms 로 옛 비용이 그대로
# 돌아왔는데 probes 는 **0** 이었다.
#
# 이유가 이 조각의 원죄와 같다. 옛 코드는 `equivalent` 를 **직접** 부르므로
# 계수되는 통로를 지나지 않는다. 계수기는 자기가 감싼 것만 본다 — W7-1 의
# `scans` 가 `equivalent` 를 못 본 것과 **정확히 같은 구조**다.
#
#   · 소스 축 ④가 그 계통의 **주 판정**이다(브라우저 코드에 equivalent 0).
#     계수기를 우회하는 새 호출은 소스로만 잡힌다.
#   · 런타임 축 ①은 **통로를 지나는 호출이 매 프레임 도는가**를 본다. 뿌리 해석
#     캐시를 걷어내는 변이가 그것이고, 그 자리는 슬롯 밖이라 델타를 따로 실어
#     보낸다(ContentsBrowserWindow::Draw).
#
# 둘 중 하나만 두면 반대쪽 변이가 조용히 지나간다.

# 시간은 판정하지 않고 기록한다 — OS 파일 캐시 온도로 흔들린다. W7 이 처음부터
# 그렇게 적었다: "판정은 `scans` 다. 시간의 2.6 배는 부차적인 증거다."

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

Write-Host ''
Write-Host '브라우저 파일시스템 계약 (PHASE 21 W7-5)'
Write-Host ''

# ══ 런타임 축 ══════════════════════════════════════════════════════════════

function Invoke-Editor([string[]]$Commands, [string]$Tag) {
    $ws = Join-Path $Work "ws-$Tag"
    New-Item -ItemType Directory -Force -Path $ws | Out-Null
    $scriptPath = Join-Path $Work "$Tag.txt"
    $resultPath = Join-Path $Work "$Tag.jsonl"
    Set-Content -LiteralPath $scriptPath -Encoding UTF8 -Value ($Commands + @('quit'))

    $priorWorkspace = $env:CREATOR_EDITOR_WORKSPACE_DIR
    $priorLegacy = $env:CREATOR_EDITOR_LEGACY_INI
    # ini 를 지운 선언 배치. W0·W2-4 와 같은 조건이고, 여기서는 특히 중요하다 —
    # 개발자의 저장 배치는 Content Browser 가 뒤 탭일 수 있고 그러면 트리가
    # **그려지지 않아** 계수가 통째로 0 이 된다. 눈먼 초록이 된다.
    $env:CREATOR_EDITOR_WORKSPACE_DIR = $ws
    $env:CREATOR_EDITOR_LEGACY_INI = Join-Path $ws 'none.ini'
    try {
        $proc = Start-Process -FilePath $Exe -WorkingDirectory (Split-Path $Exe) -WindowStyle Hidden -PassThru `
            -ArgumentList @('--script', ('"' + $scriptPath + '"'), '--result-format', 'jsonl',
                            '--result-file', ('"' + $resultPath + '"')) `
            -RedirectStandardOutput (Join-Path $Work "$Tag.out") `
            -RedirectStandardError (Join-Path $Work "$Tag.err")
        if (-not $proc.WaitForExit(600000)) { $proc.Kill(); throw "에디터가 600 초 안에 끝나지 않았다 ($Tag)." }
    }
    finally {
        if ($null -ne $priorWorkspace) { $env:CREATOR_EDITOR_WORKSPACE_DIR = $priorWorkspace }
        else { Remove-Item Env:CREATOR_EDITOR_WORKSPACE_DIR -ErrorAction SilentlyContinue }
        if ($null -ne $priorLegacy) { $env:CREATOR_EDITOR_LEGACY_INI = $priorLegacy }
        else { Remove-Item Env:CREATOR_EDITOR_LEGACY_INI -ErrorAction SilentlyContinue }
    }
    Assert (Test-Path -LiteralPath $resultPath) "결과 파일이 없다 ($Tag, 종료 코드 $($proc.ExitCode))"
    $rows = @(Get-Content -LiteralPath $resultPath | Where-Object { $_.Trim() } |
              ForEach-Object { $_ | ConvertFrom-Json })
    $failed = @($rows | Where-Object { 'succeeded' -ne $_.status })
    $failMsg = "$Tag 회차에서 실패한 명령 " + $failed.Count + ' 건: ' +
               (@($failed | ForEach-Object { $_.command + '(' + $_.status + ')' }) -join ' · ')
    Assert (0 -eq $failed.Count) $failMsg
    $exitMsg = "$Tag 회차 종료 코드가 비정상이다 (0x{0:X8})" -f $proc.ExitCode
    Assert (0 -eq $proc.ExitCode) $exitMsg
    return $rows
}

$sampleFrames = 30
$lines = [Collections.Generic.List[string]]::new()
$lines.Add('wait 240')                # 워밍업 — 첫 프레임에는 도크 구축과 최초 스캔이 섞인다
$lines.Add('editor.panelcost reset')
foreach ($i in 1..$sampleFrames) { $lines.Add('editor.dock'); $lines.Add('wait 20') }
$lines.Add('editor.panelcost')

$rows = Invoke-Editor $lines.ToArray() 'idle'
$pc = @($rows | Where-Object { 'editor.panelcost' -eq $_.command })
Assert ($pc.Count -ge 1) 'editor.panelcost 가 돌지 않았다'
$data = $pc[-1].data

$treeRows  = @($data.panels | Where-Object { 'browser_tree' -eq $_.slot })
$fileRows  = @($data.panels | Where-Object { 'browser_files' -eq $_.slot })
Assert (1 -eq $treeRows.Count) 'browser_tree 슬롯이 표에 없다'
Assert (1 -eq $fileRows.Count) 'browser_files 슬롯이 표에 없다'
$tree = $treeRows[0]
$files = $fileRows[0]

# ★ 먼저 **그려졌는지**를 단정한다. 안 그려진 패널의 probes 는 당연히 0 이고,
#   그 0 을 계약 준수로 읽으면 이 게이트는 눈먼 초록이 된다.
$treeFrameMsg = "browser_tree 가 그려진 프레임이 " + $tree.frames + " 뿐이다 — 자극하지 못했다"
Assert ($tree.frames -ge 5) $treeFrameMsg
$fileFrameMsg = "browser_files 가 그려진 프레임이 " + $files.frames + " 뿐이다 — 자극하지 못했다"
Assert ($files.frames -ge 5) $fileFrameMsg
Assert ($tree.lastUnits -ge 1) 'browser_tree 가 노드를 하나도 그리지 않았다 — 빈 트리로는 계약을 자극하지 못한다'

# ① 주 판정 — idle 프레임에 스캔 밖 디스크 접촉이 0.
$probeMsg = "browser_tree 가 idle 프레임에 디스크를 " + $tree.lastProbes +
            " 회 만졌다 (probes). 스캔이 아닌 접촉이다 — W7-5 의 그 호출이 돌아왔는지 보라."
Assert (0 -eq $tree.lastProbes) $probeMsg
$probeMsg2 = "browser_files 가 idle 프레임에 디스크를 " + $files.lastProbes + " 회 만졌다 (probes)."
Assert (0 -eq $files.lastProbes) $probeMsg2

# 누계로도 본다 — 마지막 한 프레임만 우연히 0 일 수 있다.
$totalMsg = "browser_tree 의 probes 누계가 " + $tree.totalProbes +
            " 다. 리셋 이후 " + $tree.frames + " 프레임 동안 한 번도 만지지 않아야 한다."
Assert (0 -eq $tree.totalProbes) $totalMsg
$totalMsg2 = "browser_files 의 probes 누계가 " + $files.totalProbes + ' 다.'
Assert (0 -eq $files.totalProbes) $totalMsg2

# ② W7-1 계약이 살아 있는가. 예산은 프레임당 1 이고 나이가 1 초라, 표본 구간에서
#    폴더 수만큼 갱신이 돌 수 있다. 프레임 수를 넘으면 캐시가 무너진 것이다.
$scanMsg = "browser_tree 의 scans 누계 " + $tree.totalScans + " 가 프레임 수 " +
           $tree.frames + " 를 넘는다 — W7-1 의 재스캔 예산이 무너졌다."
Assert ($tree.totalScans -le $tree.frames) $scanMsg

# ③ 기록(판정 아님).
Write-Host ('  browser_tree   frames {0,4}  units {1,4}  avg {2,7:N3} ms  p95 {3,7:N3} ms  scans {4}  probes {5}' -f `
    $tree.frames, $tree.lastUnits, $tree.avgMs, $tree.p95Ms, $tree.totalScans, $tree.totalProbes)
Write-Host ('  browser_files  frames {0,4}  units {1,4}  avg {2,7:N3} ms  p95 {3,7:N3} ms  scans {4}  probes {5}' -f `
    $files.frames, $files.lastUnits, $files.avgMs, $files.p95Ms, $files.totalScans, $files.totalProbes)

$browser = @($data.windows | Where-Object { $_.id -match 'ContentBrowser' })
if ($browser.Count -ge 1) {
    Write-Host ('  창 {0,-28} avg {1,7:N3} ms  p95 {2,7:N3} ms  (프레임 총계 avg {3:N3} ms)' -f `
        $browser[0].id, $browser[0].avgMs, $browser[0].p95Ms, $data.frame.avgMs)
}

# ══ 소스 축 ════════════════════════════════════════════════════════════════
#
# 드래그는 CLI 로 못 만들고, 새로 심긴 `equivalent` 는 표본을 비껴갈 수 있다.
# 그래서 양쪽을 다 소스에서 유도한다.

$snapshotCpp = Join-Path $repo 'Editor/EngineGUIWindow/BrowserDirectorySnapshot.cpp'
$browserCpp  = Join-Path $repo 'Editor/EngineGUIWindow/ContentsBrowserWindow.cpp'
Assert (Test-Path -LiteralPath $snapshotCpp) 'BrowserDirectorySnapshot.cpp 가 없다'
Assert (Test-Path -LiteralPath $browserCpp)  'ContentsBrowserWindow.cpp 가 없다'

# ④ `equivalent` 단일 출처. Editor 트리 **전체**에서 유도한다 — 파일을 지목해 세면
#    새 파일에 심긴 것을 통째로 못 본다.
$equivalentHits = @(
    Get-ChildItem -LiteralPath (Join-Path $repo 'Editor') -Recurse -File -Include '*.cpp','*.h' |
    ForEach-Object {
        $hitPath = $_.FullName
        $n = 0
        foreach ($line in [IO.File]::ReadAllLines($hitPath)) {
            # 주석 줄은 세지 않는다 — 설명이 대상이 되면 안 된다.
            $trimmed = $line.TrimStart()
            if ($trimmed.StartsWith('//') -or $trimmed.StartsWith('*')) { continue }
            if ($line -match '(?<![A-Za-z0-9_])(?:file|browser_fs|std::filesystem)::equivalent\s*\(') { $n++ }
        }
        if ($n -gt 0) { [pscustomobject]@{ Path = $hitPath.Substring($repo.Length + 1); Count = $n } }
    }
)
Write-Host ''
Write-Host '  equivalent 를 부르는 자리'
if (0 -eq $equivalentHits.Count) { Write-Host '    (없음)' }
foreach ($h in $equivalentHits) { Write-Host ("    {0}  x{1}" -f $h.Path, $h.Count) }

# 면제. **금지가 아니라 판정에서 빼되 수는 남긴다** — 그리고 면제 자체를 단정한다.
# 면제된 자리가 사라졌는데 목록이 남으면, 다음에 같은 이름의 새 호출이 조용히
# 그 자리를 물려받는다.
$exempt = @{
    'Editor/EngineEntry/EditorAssetDatabase.cpp' =
        '임포트할 때 원본과 대상이 같은 파일인지 보는 자리(CopyImportedFile). 여기서는 ' +
        'equivalent 의 "디스크에서 같은 파일" 의미론이 바로 필요한 것이고(제자리 임포트는 ' +
        'copy_file 이 자기 자신 위에 복사해 실패한다), 임포트 1 회당 1 회지 프레임당이 ' +
        '아니다. 매 프레임 도는 경로로 옮겨지면 이 면제를 지워라.'
}
$exemptHits = @($equivalentHits | Where-Object { $exempt.ContainsKey(($_.Path -replace '\\', '/')) })
$judgedHits = @($equivalentHits | Where-Object { -not $exempt.ContainsKey(($_.Path -replace '\\', '/')) })

Write-Host ''
foreach ($k in $exempt.Keys) {
    Write-Host ("  면제: {0}" -f $k)
    Write-Host ("        {0}" -f $exempt[$k])
    # 면제도 사실이어야 한다 — 그 자리가 실제로 남아 있는지 본다.
    $stillThere = @($exemptHits | Where-Object { ($_.Path -replace '\\', '/') -eq $k })
    $exemptMsg = "면제로 적은 " + $k + " 에 equivalent 가 없다 — 면제가 낡았으니 목록에서 지워라."
    Assert (1 -eq $stillThere.Count) $exemptMsg
}

$totalEquivalent = 0
foreach ($h in $judgedHits) { $totalEquivalent += $h.Count }
# ★ 처음엔 "단일 출처(=1)" 로 뒀는데, 고침이 끝나고 보니 그 하나를 부르는 자리가
#   **0** 이었다 — 계수되는 통로로 두려고 만든 함수인데 통로를 지나는 것이 없었다.
#   걷고 나니 더 센 것을 세울 수 있다: 브라우저 코드에 equivalent 가 **하나도
#   없다.** 남겨 뒀다면 "하나만 있다" 밖에 못 세우고, 그 하나를 다시 부르기
#   시작하는 변이를 소스 축이 못 잡았을 것이다(idle 에는 드래그가 없어 런타임
#   축도 못 잡는다).
$srcMsg = "면제를 뺀 equivalent 호출이 " + $totalEquivalent +
          " 자리다 — 브라우저 코드에는 0 이어야 한다. 경로 비교는 어휘로 하고" +
          "(browser_same_directory), 디스크 철자가 필요하면 browser_canonical 로 미리 한 번 풀어 들고 있어라. " +
          "찾은 자리: " + (@($judgedHits | ForEach-Object { $_.Path }) -join ' · ')
Assert (0 -eq $totalEquivalent) $srcMsg

# ⑤ 계수가 호출 **앞**에 있다. 뒤에 두면 실패한 호출이 계수기를 빠져나간다.
$snapshotText = [IO.File]::ReadAllText($snapshotCpp)
# 디스크를 만지는 통로 둘. 각각 자기 syscall **앞**에서 세야 한다 — 뒤에 두면
# 실패한 호출이 계수기를 빠져나가고, 없는 경로를 묻는 자리가 통째로 안 보인다.
$countedPaths = @(
    @{ Helper = 'browser_canonical';        Call = 'browser_fs::weakly_canonical' },
    @{ Helper = 'browser_directory_exists'; Call = 'browser_fs::is_directory' }
)
foreach ($c in $countedPaths) {
    $helperIndex = $snapshotText.IndexOf($c.Helper + '(const browser_fs::path& path')
    $helperMsg = $c.Helper + ' 정의를 BrowserDirectorySnapshot.cpp 에서 찾지 못했다'
    Assert ($helperIndex -ge 0) $helperMsg
    $bodyTail = $snapshotText.Substring($helperIndex)
    $probeOffset = $bodyTail.IndexOf('++state().stats.probes')
    $callOffset  = $bodyTail.IndexOf($c.Call)
    $probeMissMsg = $c.Helper + ' 안에서 probes 계수 구문을 찾지 못했다 — 이 통로가 계수 밖이다'
    Assert ($probeOffset -ge 0) $probeMissMsg
    $callMissMsg = $c.Helper + ' 안에서 ' + $c.Call + ' 호출을 찾지 못했다'
    Assert ($callOffset -ge 0) $callMissMsg
    $orderMsg = $c.Helper + ' 의 probes 계수가 ' + $c.Call + ' 호출 뒤에 있다 — 실패한 호출이 계수기를 빠져나간다'
    Assert ($probeOffset -lt $callOffset) $orderMsg
}

# ⑥ 드래그 관문이 경로 비교보다 앞. 두 자리 모두 본다.
$browserLines = [IO.File]::ReadAllLines($browserCpp)
$targetLines = @()
for ($i = 0; $i -lt $browserLines.Count; $i++) {
    # ★ 주석 줄을 빼지 않으면 **이 게이트를 설명하는 주석이 대상이 된다.**
    #   실제로 한 번 그렇게 세어 관문이 셋으로 잡혔다 — 자기 설명을 코드로 읽은 것이다.
    $t = $browserLines[$i].TrimStart()
    if ($t.StartsWith('//') -or $t.StartsWith('*')) { continue }
    if ($browserLines[$i] -match 'BeginDragDropTarget\s*\(') { $targetLines += $i }
}
$dragMsg = "BeginDragDropTarget 을 부르는 자리가 " + $targetLines.Count + " 개다 — 둘을 기대한다(트리·파일 목록)."
Assert (2 -eq $targetLines.Count) $dragMsg
foreach ($idx in $targetLines) {
    $line = $browserLines[$idx]
    $sameOnLine = ($line -match 'browser_same_path')
    $orderMsg = "ContentsBrowserWindow.cpp:" + ($idx + 1) +
                " 에서 경로 비교가 BeginDragDropTarget 과 같은 줄에 있다 — && 왼쪽이면 드래그가 없어도 매 프레임 돈다. " +
                "관문을 앞에 두고 비교를 블록 안으로 옮겨라. 줄: " + $line.Trim()
    Assert (-not $sameOnLine) $orderMsg
    # 관문 바로 다음 줄들 안에서 비교가 이뤄져야 한다(블록 안).
    $found = $false
    for ($j = $idx + 1; $j -lt [Math]::Min($idx + 6, $browserLines.Count); $j++) {
        if ($browserLines[$j] -match 'browser_same_path') { $found = $true; break }
    }
    $insideMsg = "ContentsBrowserWindow.cpp:" + ($idx + 1) +
                 " 의 드래그 블록 안에서 경로 비교를 찾지 못했다 — 검사가 통째로 사라졌는지 보라."
    Assert $found $insideMsg
}

# ⑦ 디스크를 만지는 통로 둘이 **실제로 불리는지** 본다. 아무도 안 부르면
#    그 함수는 죽은 코드이고, 죽은 통로를 지키는 계수기는 빈 단정이다.
$browserAll = [IO.File]::ReadAllText($browserCpp)
foreach ($c in $countedPaths) {
    $used = ([regex]::Matches($browserAll, '::' + $c.Helper + '\s*\(')).Count
    Write-Host ("  {0} 호출자: {1}" -f $c.Helper, $used)
    $usedMsg = $c.Helper + ' 을 부르는 자리가 0 이다 — 소비자 없는 통로이고, 그것을 지키는 계수기는 빈 단정이다.'
    Assert ($used -ge 1) $usedMsg
}

# ⑧⑨ 계측 자체가 살아 있는가.
#
# ★ 런타임 축만 두면 **계측을 죽이는 변이가 초록으로 지나간다** — `add_panel_probes`
#   배선을 지우면 슬롯 probes 는 영원히 0 이고 ①은 그것을 계약 준수로 읽는다.
#   슬롯 축을 0 이 아닌 값으로 만드는 자극은 드래그뿐이라 CLI 로 만들 수 없다.
#   그래서 배선의 **존재**를 소스에서 단정한다(W2-3 이 "선언한 것이 전부 관측됐다"
#   로 같은 구멍을 막은 것과 같은 계통이다).
#
# ★ 개수가 아니라 **이름**으로 센다. 처음엔 "두 자리" 로 못 박았는데 뿌리 해석의
#   델타를 실어 보내면서 셋이 됐고, 숫자만 보는 단정은 그때 붉어질 뿐 *어느* 배선이
#   사라졌는지 말해 주지 못한다. 자리를 함수 이름으로 유도해 집합을 맞대면 지워진
#   쪽의 이름이 그대로 찍힌다.
$browserText = [IO.File]::ReadAllText($browserCpp)
$expectedWiring = @('DrawDirectoryPanel', 'ShowCurrentDirectoryFiles', 'Draw')
# 배선이 선 자리마다, 그 **앞쪽에서 가장 가까운 함수 머리**의 이름을 집는다.
# 줄 단위로 상태를 들고 도는 대신 오프셋으로 되짚는다 — 상태를 들고 도는 순회는
# 한 줄만 어긋나도 이름이 통째로 빈 문자열이 되고, 그러면 "전부 빠졌다" 로 보인다.
$headerMatches = [regex]::Matches($browserText, 'ContentsBrowserWindow::([A-Za-z_][A-Za-z0-9_]*)\s*\(')
$wiredFunctions = @{}
foreach ($w in [regex]::Matches($browserText, 'add_panel_probes\s*\(')) {
    $owner = ''
    foreach ($h in $headerMatches) {
        if ($h.Index -lt $w.Index) { $owner = $h.Groups[1].Value } else { break }
    }
    $wiredFunctions[$owner] = $true
}
$wiredNames = @($wiredFunctions.Keys | Sort-Object)
Write-Host ("  add_panel_probes 배선이 선 함수: " + ($wiredNames -join ' · '))
$missing = @($expectedWiring | Where-Object { -not $wiredFunctions.ContainsKey($_) })
$extra   = @($wiredNames | Where-Object { $_ -notin $expectedWiring })
$missingText = if (0 -eq $missing.Count) { '(없음)' } else { $missing -join ' · ' }
$extraText   = if (0 -eq $extra.Count)   { '(없음)' } else { $extra -join ' · ' }
$wiringMsg = "add_panel_probes 배선이 어긋난다 — 빠진 함수: " + $missingText +
             " · 예상 밖 함수: " + $extraText +
             ". 배선이 없으면 그 슬롯의 probes 는 영원히 0 이고 ①이 눈먼다."
Assert ((0 -eq $missing.Count) -and (0 -eq $extra.Count)) $wiringMsg

$counterCount = ([regex]::Matches($snapshotText, '\+\+state\(\)\.stats\.probes')).Count
$counterMsg = "전역 probes 계수 구문이 " + $counterCount + " 자리인데 디스크를 만지는 통로는 " +
              $countedPaths.Count + " 개다 — 통로마다 정확히 하나여야 한다. " +
              "많으면 한 호출이 두 번 세지고, 적으면 세지 않는 통로가 있다."
Assert ($countedPaths.Count -eq $counterCount) $counterMsg

Write-Host ''
Write-Host ("브라우저 파일시스템 계약 OK — 단정 {0} 건 · idle 프레임 probes 0 · 노드 {1} 개" -f `
    $script:checks, $tree.lastUnits)
exit 0
