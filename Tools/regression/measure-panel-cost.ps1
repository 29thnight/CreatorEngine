[CmdletBinding()]
param(
    [string]$Exe = (Join-Path $PSScriptRoot '../../Bin/x64-Release/Editor/CreatorEditor.exe'),
    [string]$Work = (Join-Path $env:TEMP 'creator-panel-cost'),
    # ★ 배열이 아니라 문자열이다. `pwsh -File` 은 `-Counts 1000,10000,50000` 을
    #   통째로 한 문자열로 넘기고, 공백으로 띄우면 뒤엣것이 다음 위치 인자로
    #   빨려 들어간다(실제로 `-Exe` 가 "10000" 이 됐다). 이 저장소가 `-Only a,b`
    #   로 이미 한 번 데인 함정이라, 받는 쪽에서 쪼갠다.
    [string]$Counts = '1000,10000,50000',
    [int]$Fanout = 0,
    # 2400x1400 인 이유는 브라우저의 **폴더 트리가 접히지 않는 최소 폭** 때문이다.
    # `ContentsBrowserWindow` 는 자기 폭이 560 logical px 미만이면 트리를 숨기고,
    # 1600 창에서는 아래 도크가 그 밑으로 떨어져 browser_tree 가 0 으로 나왔다.
    [int]$WindowWidth = 2400,
    [int]$WindowHeight = 1400,
    [string]$Label = '',
    # W2-B·W7 공동 fixture — 엔티티 N 개와 **같은 회차에** 자산 파일 N 개를 한 폴더에
    # 올리고 브라우저 비용·잘라 그리기·가시 썸네일 요청을 함께 잰다.
    [switch]$WithAssets
)
# PHASE 21 W7-0 — Hierarchy/Browser draw 비용 **기준선 측정 도구**.
#
# 게이트가 아니다. run-all 에 넣지 않는다 — 이것은 판정이 아니라 수치를 남기는
# 자다. W7 의 완료 기준이 "p95 CPU·allocation 개선 수치를 기록한다. 캐시만
# 추가하고 실측 이득이 없으면 제거한다" 이므로, 캐시를 넣기 **전에** 이 수치가
# 있어야 한다.
#
# ★ Release 로만 판정한다. 이 저장소는 Debug 로 재서 "해상도는 무관하다" 를
#   보고한 적이 있다 — Debug 의 CPU 비용이 재려는 축을 통째로 덮는다. 기본
#   -Exe 가 x64-Release 인 것은 그래서다.
#
# ★ `wait N` 으로 프레임을 세지 않는다. `wait` 는 **게임 스레드** 프레임이고
#   패널은 Presentation 스레드가 그린다. 대신 `editor.panelcost` 가 내는
#   `samples`(링에 담긴 표본 수)가 원하는 만큼 쌓일 때까지 **표집한다**.
#
# fixture 는 저작 자산이 아니라 제품 경로(`scene.populate` → `Scene::CreateEntity`)
# 로 만들고, 만든 직후 `scene.hierarchycheck` 로 고아·쌍불일치·순회미도달 0 을
# 확인한다. 그 줄이 붉으면 측정값은 읽지 않는다 — 무엇을 그렸는지 모르는 수치다.
#
# `-WithAssets` 는 W2-B 의 "1k/10k/50k 목록의 비용·clipping·가시 썸네일 요청" 을 같은
# 회차에 잰다. 추적되는 `fixtures/browser-thumbnails/Tiny4.png` 를 N 개 복사해
# `Dynamic_CPP/Assets/W7ScaleAssets_<난수>` 에 올리고(감시자가 `.meta` 를 N 개 만든다),
# 끝나면 폴더째 지운다. 구간은 넷이다:
#   ① 대기 — 그 폴더에 선 채 표집. 결과 기억이 있어 다시 모으지 않는다.
#   ② 끝까지 스크롤 — 잘라 그리기가 마지막 줄에서도 보이는 줄만 그리는지, 썸네일
#      요청이 새로 보인 타일만큼만 느는지.
#   ③ 검색 되풀이 — 검색어를 넣고 빼기를 되풀이해 **다시 모으는 프레임**의 비용을
#      max 로 드러낸다(대기의 p95 는 결과 기억 덕이라 이 비용을 가린다).
#   ④ 전체 자산 — `@everything` 이 다 찬 뒤 대기 표집.
# 게이트가 아니므로 붉게 끝내지 않는다. 다만 units 가 N 에 따라 늘거나 요청이 보이는
# 타일 수를 넘으면 ★ 로 적는다.
#
# 사용법:
#   pwsh Tools/regression/measure-panel-cost.ps1
#   pwsh Tools/regression/measure-panel-cost.ps1 -Counts 1000,10000 -Fanout 4
#   pwsh Tools/regression/measure-panel-cost.ps1 -WithAssets
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$Exe = [IO.Path]::GetFullPath($Exe)
$Work = [IO.Path]::GetFullPath($Work)
if (-not (Test-Path -LiteralPath $Exe)) { throw "Editor executable not found: $Exe" }
if (Get-Process CreatorEditor -ErrorAction SilentlyContinue) {
    throw '이미 떠 있는 에디터를 닫아라 — endpoint 파일은 프로젝트에 하나다.'
}
New-Item -ItemType Directory -Force -Path $Work | Out-Null
$script:assetsRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../../Dynamic_CPP/Assets'))
$script:tinyPng = $null
$script:recentsPath = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../../Dynamic_CPP/Library/EditorState/ContentBrowserRecents.txt'))
$script:recentsBackup = ''
if ($WithAssets) {
    $script:tinyPng = [IO.File]::ReadAllBytes((Join-Path $PSScriptRoot 'fixtures/browser-thumbnails/Tiny4.png'))
    # 방문 폴더가 최근 항목에 남는다 — 개발자의 목록을 되돌려 놓는다.
    if (Test-Path -LiteralPath $script:recentsPath) {
        $script:recentsBackup = [Convert]::ToBase64String([IO.File]::ReadAllBytes($script:recentsPath))
    }
}

# 표본이 이만큼 쌓일 때까지 표집한다. 링 용량(512)보다 작아야 평균·p95 가
# 이 측정만의 것이 된다.
$kTargetSamples = 240
$kMaxPolls = 40

function Invoke-Measurement([int]$count) {
    $tag = "n$count-f$Fanout"
    $scriptPath = Join-Path $Work "panelcost-$tag.txt"
    $resultPath = Join-Path $Work "panelcost-$tag.jsonl"
    $stdoutPath = Join-Path $Work "panelcost-$tag.out"
    $stderrPath = Join-Path $Work "panelcost-$tag.err"
    foreach ($file in @($resultPath, $stdoutPath, $stderrPath)) {
        if (Test-Path -LiteralPath $file) { Remove-Item -LiteralPath $file }
    }

    $assetFolder = $null
    $assetName = $null
    if ($WithAssets) {
        $assetName = 'W7ScaleAssets_' + [guid]::NewGuid().ToString('N').Substring(0, 8)
        $assetFolder = Join-Path $script:assetsRoot $assetName
        New-Item -ItemType Directory -Force -Path $assetFolder | Out-Null
        for ($n = 0; $n -lt $count; $n++) {
            [IO.File]::WriteAllBytes((Join-Path $assetFolder ('T{0:D5}.png' -f $n)), $script:tinyPng)
        }
    }

    $lines = [Collections.Generic.List[string]]::new()
    # 줄 번호 → 구간 이름. 결과 파일은 줄마다 한 행이라 번호로 짝을 짓는다.
    $marks = @{}
    function Mark([string]$name) { $marks[$lines.Count - 1] = $name }
    if ($WithAssets) {
        # ★ 썸네일 장부는 부팅 앞에서 비운다 — 뒤에 두면 첫 요청이 버려진다.
        $lines.Add('editor.thumbnail reset')
    }
    $lines.AddRange([string[]]@(
        # 창 크기를 고정한다 — 보이는 행 수가 기계마다 다르면 units 를 비교할 수 없다.
        "window.resize $WindowWidth $WindowHeight",
        'wait 120',
        'scene.new W7Fixture',
        'wait 60',
        "scene.populate $count $Fanout",
        'scene.hierarchycheck',
        # Content Browser 는 AssetBundle·ResourceCounter 와 같은 dock_slot::bottom
        # 이라 **탭으로 겹친다**. 열려 있어도 선택되지 않으면 본문이 돌지 않아
        # 처음 재었을 때 181 프레임 중 1 프레임만 잡혔다. 앞으로 세운다.
        'editor.window ###Editor.ContentBrowser focus',
        'wait 120'
    ))
    # ★ 이동은 브라우저가 한 번 그려진 뒤에 건다. 첫 줄에 두면 `not_drawn` 으로
    #   거절 응답이 나와 종료 코드가 4 가 된다(요청 자체는 적용된다).
    if ($WithAssets) { $lines.Add("editor.browser go $assetName"); $lines.Add('wait 120') }
    $lines.Add('editor.panelcost reset')
    for ($p = 0; $p -lt $kMaxPolls; $p++) { $lines.Add('wait 60'); $lines.Add('editor.panelcost'); Mark 'idle-cost' }
    if ($WithAssets) {
        $lines.Add('editor.thumbnail'); Mark 'idle-thumb'
        $lines.Add('editor.browser'); Mark 'idle-browser'

        $lines.Add('editor.panelcost reset')
        $lines.Add('editor.browser scroll 100000000')
        for ($p = 0; $p -lt 4; $p++) { $lines.Add('wait 60') }
        $lines.Add('editor.panelcost'); Mark 'end-cost'
        $lines.Add('editor.thumbnail'); Mark 'end-thumb'
        $lines.Add('editor.browser'); Mark 'end-browser'

        $lines.Add('editor.browser scroll 0')
        $lines.Add('wait 60')
        $lines.Add('editor.panelcost reset')
        $lines.Add('editor.browser'); Mark 'search-before'
        for ($p = 0; $p -lt 40; $p++) {
            $lines.Add('editor.browser search T0'); $lines.Add('wait 3')
            $lines.Add('editor.browser search'); $lines.Add('wait 3')
        }
        $lines.Add('editor.panelcost'); Mark 'search-cost'
        $lines.Add('editor.browser'); Mark 'search-after'

        $lines.Add('editor.browser go @everything')
        for ($p = 0; $p -lt 20; $p++) { $lines.Add('wait 60'); $lines.Add('editor.browser'); Mark 'everything-fill' }
        $lines.Add('editor.panelcost reset')
        for ($p = 0; $p -lt 6; $p++) { $lines.Add('wait 60') }
        $lines.Add('editor.panelcost'); Mark 'everything-cost'
        $lines.Add('editor.browser'); Mark 'everything-browser'
    }
    $lines.Add('quit')
    Set-Content -LiteralPath $scriptPath -Encoding UTF8 -Value $lines.ToArray()

    # 워크스페이스를 격리한다 — 개발자의 배치를 건드리지 않고, 기본 배치라야
    # Hierarchy·Content Browser 가 둘 다 열려 있다.
    $workspaceDir = Join-Path $Work "workspace-$tag"
    if (Test-Path -LiteralPath $workspaceDir) { Remove-Item -LiteralPath $workspaceDir -Recurse -Force }
    New-Item -ItemType Directory -Force -Path $workspaceDir | Out-Null
    $savedWorkspaceDir = $env:CREATOR_EDITOR_WORKSPACE_DIR
    $savedLegacyIni = $env:CREATOR_EDITOR_LEGACY_INI
    $env:CREATOR_EDITOR_WORKSPACE_DIR = $workspaceDir
    $env:CREATOR_EDITOR_LEGACY_INI = Join-Path $workspaceDir 'none.ini'
    try {
        $proc = Start-Process -FilePath $Exe -WorkingDirectory (Split-Path $Exe) -WindowStyle Hidden -PassThru `
            -ArgumentList @('--script', ('"' + $scriptPath + '"'), '--result-format', 'jsonl',
                            '--result-file', ('"' + $resultPath + '"')) `
            -RedirectStandardOutput $stdoutPath -RedirectStandardError $stderrPath
        if (-not $proc.WaitForExit(900000)) { $proc.Kill(); throw "editor did not exit in time ($tag)" }
        $exitCode = $proc.ExitCode
    }
    finally {
        if ($null -ne $savedWorkspaceDir) { $env:CREATOR_EDITOR_WORKSPACE_DIR = $savedWorkspaceDir }
        else { Remove-Item Env:CREATOR_EDITOR_WORKSPACE_DIR -ErrorAction SilentlyContinue }
        if ($null -ne $savedLegacyIni) { $env:CREATOR_EDITOR_LEGACY_INI = $savedLegacyIni }
        else { Remove-Item Env:CREATOR_EDITOR_LEGACY_INI -ErrorAction SilentlyContinue }
        if ($null -ne $assetFolder) {
            Remove-Item -LiteralPath $assetFolder -Recurse -Force -ErrorAction SilentlyContinue
            Remove-Item -LiteralPath "$assetFolder.meta" -Force -ErrorAction SilentlyContinue
        }
    }

    if (-not (Test-Path -LiteralPath $resultPath)) { throw "result file missing: $resultPath ($tag)" }
    $rows = @(Get-Content -LiteralPath $resultPath | Where-Object { $_.Trim() } | ForEach-Object { $_ | ConvertFrom-Json })

    $populate = @($rows | Where-Object { $_.command -eq 'scene.populate' }) | Select-Object -First 1
    $check    = @($rows | Where-Object { $_.command -eq 'scene.hierarchycheck' }) | Select-Object -First 1
    $costs    = @($rows | Where-Object { $_.command -eq 'editor.panelcost' })

    if ($null -eq $populate -or $populate.status -ne 'succeeded') {
        throw "scene.populate 가 실패했다 ($tag): $($populate.code) $($populate.message)"
    }
    if ($null -eq $check -or $check.status -ne 'succeeded') {
        # fixture 가 계층 불변식을 깨뜨렸다면 측정값은 읽지 않는다.
        throw "scene.hierarchycheck 가 붉다 ($tag): $($check.code) $($check.message)"
    }

    # 표본이 목표만큼 쌓인 마지막 표집을 쓴다. 없으면 가장 많이 쌓인 것을 쓰고
    # 그 사실을 함께 낸다 — 조용히 적은 표본으로 p95 를 말하지 않는다.
    # 대기 구간의 표집만 본다 — 뒤 구간은 링을 다시 비운다.
    $best = $null
    $costs = @(for ($k = 0; $k -lt $rows.Count; $k++) { if ($marks[$k] -eq 'idle-cost') { $rows[$k] } })
    foreach ($row in $costs) {
        if ($row.status -ne 'succeeded') { continue }
        $hierarchy = @($row.data.panels | Where-Object { $_.slot -eq 'hierarchy' }) | Select-Object -First 1
        if ($null -eq $hierarchy) { continue }
        if ($null -eq $best) { $best = $row; continue }
        $bestHierarchy = @($best.data.panels | Where-Object { $_.slot -eq 'hierarchy' }) | Select-Object -First 1
        if ($hierarchy.samples -gt $bestHierarchy.samples) { $best = $row }
    }
    if ($null -eq $best) { throw "editor.panelcost 표본이 하나도 없다 ($tag)" }

    [pscustomobject]@{
        Count       = $count
        Fanout      = $Fanout
        Created     = $populate.data.created
        MaxDepth    = $populate.data.maxDepth
        PopulateMs  = [math]::Round([double]$populate.data.elapsedMs, 1)
        Frames      = $best.data.publishedFrames
        Panels      = $best.data.panels
        ExitCode    = $exitCode
        ResultPath  = $resultPath
        Assets      = if ($WithAssets) { Read-AssetArms $rows $marks $count } else { $null }
    }
}

function Read-AssetArms($rows, $marks, [int]$count) {
    $at = @{}
    $fill = @()
    for ($k = 0; $k -lt $rows.Count; $k++) {
        if (-not $marks.ContainsKey($k)) { continue }
        if ($marks[$k] -eq 'everything-fill') { $fill += $rows[$k]; continue }
        $at[$marks[$k]] = $rows[$k]
    }
    foreach ($need in @('idle-thumb', 'idle-browser', 'end-cost', 'end-thumb', 'end-browser',
                        'search-before', 'search-cost', 'search-after', 'everything-cost', 'everything-browser')) {
        if (-not $at.ContainsKey($need) -or $at[$need].status -ne 'succeeded') {
            throw "자산 구간 행 '$need' 가 없거나 실패했다 (n=$count)"
        }
    }
    function Files($row) { @($row.data.panels | Where-Object { $_.slot -eq 'browser_files' }) | Select-Object -First 1 }
    $idleCosts = @(for ($k = 0; $k -lt $rows.Count; $k++) { if ($marks[$k] -eq 'idle-cost' -and $rows[$k].status -eq 'succeeded') { $rows[$k] } })
    $idleFiles = Files ($idleCosts | Sort-Object { (Files $_).samples } | Select-Object -Last 1)
    $completeAt = @($fill | Where-Object { $_.data.everythingComplete }).Count
    [pscustomobject]@{
        IdleResults     = $at['idle-browser'].data.resultCount
        IdleFiles       = $idleFiles
        IdleRequests    = $at['idle-thumb'].data.requests
        EndFiles        = Files $at['end-cost']
        EndRequests     = $at['end-thumb'].data.requests
        EndEntries      = $at['end-thumb'].data.entries
        SearchFiles     = Files $at['search-cost']
        SearchRebuilds  = [long]$at['search-after'].data.resultRebuilds - [long]$at['search-before'].data.resultRebuilds
        EverythingFiles = Files $at['everything-cost']
        EverythingResults  = $at['everything-browser'].data.resultCount
        EverythingComplete = [bool]$at['everything-browser'].data.everythingComplete
        EverythingFillPolls = $fill.Count - $completeAt
    }
}

$stamp = Get-Date -Format 'yyyy-MM-dd HH:mm:ss'
$config = if ($Exe -match '(?i)x64-Release') { 'Release' } elseif ($Exe -match '(?i)x64-Debug') { 'Debug' } else { 'unknown' }
Write-Host ''
Write-Host "패널 비용 기준선 — $stamp · $config · 창 ${WindowWidth}x${WindowHeight} · fanout $Fanout $Label"
if ($config -ne 'Release') {
    Write-Host '  ★ Release 가 아니다. 이 수치는 성능 판정의 근거가 되지 못한다.' -ForegroundColor Yellow
}

$countList = @($Counts -split '[,;\s]+' | Where-Object { $_ } | ForEach-Object { [int]$_ })
if ($countList.Count -eq 0) { throw "-Counts 가 비었다: '$Counts'" }

$results = @()
try {
    foreach ($count in $countList) { $results += Invoke-Measurement $count }
}
finally {
    if ($WithAssets) {
        if ($script:recentsBackup) {
            [IO.File]::WriteAllBytes($script:recentsPath, [Convert]::FromBase64String($script:recentsBackup))
        }
        elseif (Test-Path -LiteralPath $script:recentsPath) { Remove-Item -LiteralPath $script:recentsPath }
    }
}

Write-Host ''
Write-Host ('{0,-8} {1,-14} {2,8} {3,8} {4,8} {5,9} {6,8} {7,7}' -f `
    'entities', 'panel', 'lastMs', 'avgMs', 'p95Ms', 'maxMs', 'units', 'scans')
Write-Host ('-' * 78)
foreach ($result in $results) {
    foreach ($panel in $result.Panels) {
        Write-Host ('{0,-8} {1,-14} {2,8:N3} {3,8:N3} {4,8:N3} {5,9:N3} {6,8} {7,7}' -f `
            $result.Count, $panel.slot, $panel.lastMs, $panel.avgMs, $panel.p95Ms, $panel.maxMs,
            $panel.lastUnits, $panel.lastScans)
    }
    $hierarchy = @($result.Panels | Where-Object { $_.slot -eq 'hierarchy' }) | Select-Object -First 1
    $samples = if ($null -ne $hierarchy) { $hierarchy.samples } else { 0 }
    Write-Host ("         (created=$($result.Created) depth=$($result.MaxDepth) " +
                "populate=$($result.PopulateMs)ms uiFrames=$($result.Frames) samples=$samples exit=$($result.ExitCode))")
    if ($samples -lt $kTargetSamples) {
        Write-Host ("         ★ 표본 $samples < 목표 $kTargetSamples — p95 를 이 수로 말한다") -ForegroundColor Yellow
    }
}
Write-Host ''

if ($WithAssets) {
    Write-Host '자산 공동 fixture — browser_files (한 폴더에 PNG N 개)'
    Write-Host ('{0,-8} {1,-12} {2,8} {3,8} {4,9} {5,6} {6,9} {7,9}' -f `
        'files', 'arm', 'results', 'p95Ms', 'maxMs', 'units', 'requests', 'etc')
    Write-Host ('-' * 78)
    $firstUnits = $null
    foreach ($result in $results) {
        $a = $result.Assets
        $rowsOut = @(
            @('대기', $a.IdleResults, $a.IdleFiles, $a.IdleRequests, "samples=$($a.IdleFiles.samples)"),
            @('끝 스크롤', $a.IdleResults, $a.EndFiles, $a.EndRequests, "entries=$($a.EndEntries)"),
            @('검색 되풀이', '', $a.SearchFiles, '', "rebuilds=$($a.SearchRebuilds)"),
            @('전체 자산', $a.EverythingResults, $a.EverythingFiles, '', "fillPolls=$($a.EverythingFillPolls)")
        )
        foreach ($r in $rowsOut) {
            Write-Host ('{0,-8} {1,-12} {2,8} {3,8:N3} {4,9:N3} {5,6} {6,9} {7}' -f `
                $result.Count, $r[0], $r[1], [double]$r[2].p95Ms, [double]$r[2].maxMs, $r[2].lastUnits, $r[3], $r[4])
        }
        if ($a.IdleResults -ne $result.Count) {
            Write-Host "         ★ 결과 $($a.IdleResults) 이 파일 수 $($result.Count) 와 다르다 — 무엇을 잰 것인지 모른다" -ForegroundColor Yellow
        }
        if (-not $a.EverythingComplete) {
            Write-Host '         ★ 전체 자산이 다 차지 않은 채 쟀다' -ForegroundColor Yellow
        }
        if ($null -eq $firstUnits) { $firstUnits = [int]$a.IdleFiles.lastUnits }
        elseif ([int]$a.IdleFiles.lastUnits -gt $firstUnits) {
            Write-Host "         ★ 그린 줄이 $firstUnits → $($a.IdleFiles.lastUnits) 로 늘었다 — 잘라 그리기가 N 을 따른다" -ForegroundColor Yellow
        }
        if ([int]$a.EndRequests -gt 4 * [int]$a.IdleRequests) {
            Write-Host "         ★ 끝까지 스크롤한 뒤 요청 $($a.EndRequests) — 보이는 타일보다 훨씬 많다" -ForegroundColor Yellow
        }
    }
    Write-Host ''
}
