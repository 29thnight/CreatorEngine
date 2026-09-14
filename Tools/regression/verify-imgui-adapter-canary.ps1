[CmdletBinding()]
param(
    [string]$Exe = (Join-Path $PSScriptRoot '../../Bin/x64-Release/Editor/CreatorEditor.exe'),
    [string]$Work = (Join-Path $env:TEMP 'creator-imgui-canary')
)
# PHASE 21 W8 — ImGui 내부 API 어댑터의 판 canary와 legacy 잔재 재유입.
#
# 계획서 W8 의 두 줄이다: *"ImGui internal adapter version canary를 CI에 넣는다"* 와
# *"이미 제거된 `BuildInitialDockLayout`의 ContentsBrowserStyle 분기가 재유입되지
# 않는지 확인하고, 핵심 ToolPanel `NoMove` 잔재를 정리한다."*
#
# ── 왜 판을 못 박아야 하는가 ──────────────────────────────────────────────
#
# 도크 노드와 창의 도크 소속은 `imgui.h` 의 공개 면에 없다. `imgui_internal.h` 의
# `ImGuiDockNode`·`ImGuiWindow` 를 직접 읽어야 하고, 그 구조체는 판마다 바뀐다.
# 바뀐 판에서도 **컴파일은 통과하고 읽는 값만 어긋나는** 사고가 이 어댑터의 고유
# 위험이다.
#
# ── 이 게이트가 세우는 세 문장 ────────────────────────────────────────────
#
#  ① **판이 한 곳에 못 박혀 있고 모든 자리가 같은 수를 말한다.** 소스에 흩어진
#     판 상수를 전부 뽑아 맞댄다. 양쪽을 다 소스에서 유도한다 — 한쪽을 이 파일에
#     적어 두면 제품이 판을 올릴 때 게이트가 조용히 낡는다(W6-2 에서 실제로
#     `CreatorWorkspace 1` 이 그랬다).
#  ② **헤더가 말한 판과 라이브러리가 말한 판이 같다.** 앞의 둘은 전처리기가 박은
#     값이라 헤더만 증언한다. 이 기계에는 imgui 설치본이 둘이고 판이 다르므로,
#     헤더를 A 에서 라이브러리를 B 에서 가져오는 일이 실제로 가능하다. 그래서
#     `ImGui::GetVersion()` — 도는 코드가 말하는 값 — 을 함께 본다.
#     `IMGUI_CHECKVERSION()` 만으로는 못 막는다: 그 안은 `IM_ASSERT` 로만 말하는데
#     그것이 `assert()` 이고 Release 는 NDEBUG 라 통째로 사라진다.
#  ③ **legacy 잔재가 다시 들어오지 않는다.** `ContentsBrowserStyle` 분기는 이미
#     죽었고, `no_move` 는 중앙 뷰포트에만 있어야 한다.
#
# ── 못 잡는 것 ────────────────────────────────────────────────────────────
#
# 구조체 **배치**가 갈리는 것은 못 본다. 판 번호가 같은데 imconfig 가 달라 배치가
# 갈리는 경우가 그것이고, 그 자리는 제품 쪽 `IMGUI_CHECKVERSION()` 반환값 검사가
# 맡는다(그 줄이 던지므로 에디터가 아예 안 뜬다). 여기서는 에디터가 떴다는 사실로
# 간접 확인할 뿐이다.
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$Exe = [IO.Path]::GetFullPath($Exe)
$Work = [IO.Path]::GetFullPath($Work)
$repo = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
if (-not (Test-Path -LiteralPath $Exe)) { throw "Editor executable not found: $Exe" }
if (Get-Process CreatorEditor -ErrorAction SilentlyContinue) {
    throw '이미 떠 있는 에디터를 닫아라 — endpoint 파일은 프로젝트에 하나다.'
}

$script:checks = 0
function Assert([bool]$Condition, [string]$Message) {
    if (-not $Condition) { throw $Message }
    $script:checks++
}

function Read-Source([string]$relative) {
    $path = Join-Path $repo $relative
    Assert (Test-Path -LiteralPath $path) "소스를 못 찾았다: $relative"
    [IO.File]::ReadAllText($path)
}

Write-Host ''
Write-Host 'ImGui 어댑터 판 canary (PHASE 21 W8)'
Write-Host ''

# ── ① 소스에 흩어진 판 상수를 전부 뽑아 맞댄다 ────────────────────────────
#
# 개수를 못 박지 않는다. 자리가 늘어도 게이트가 깨지지 않되, **하나도 없으면**
# 붉어야 한다 — 빈 집합을 성공으로 읽는 것이 이 저장소가 여러 번 데인 양식이다.
$editorRoot = Join-Path $repo 'Editor'
$sources = @(Get-ChildItem -LiteralPath $editorRoot -Recurse -File -Include *.cpp, *.h |
             Where-Object { $_.FullName -notmatch '\\Build\\' })
Assert ($sources.Count -gt 0) 'Editor 소스를 하나도 못 찾았다 — 경로가 틀렸다'

$pinned = @()          # 판을 못 박은 자리 전부: @{ File; Value; Kind }
foreach ($file in $sources) {
    $text = [IO.File]::ReadAllText($file.FullName)
    foreach ($m in [regex]::Matches($text,
        'static_assert\s*\(\s*IMGUI_VERSION_NUM\s*==\s*(?<v>\d+)')) {
        $pinned += [pscustomobject]@{
            File = $file.FullName.Substring($repo.Length + 1); Value = [int]$m.Groups['v'].Value; Kind = 'static_assert' }
    }
    foreach ($m in [regex]::Matches($text,
        'expected_imgui_version_num\s*=\s*(?<v>\d+)')) {
        $pinned += [pscustomobject]@{
            File = $file.FullName.Substring($repo.Length + 1); Value = [int]$m.Groups['v'].Value; Kind = 'expected' }
    }
}

# 유도한 집합의 **이름을 전부 찍는다.** 대조가 맞았다고 대상이 온전한 것은
# 아니다 — 파서가 양쪽에서 똑같이 눈멀면 0 대 0 으로 초록이 된다.
foreach ($one in $pinned) {
    Write-Host ("  판 고정: {0,-14} {1} = {2}" -f $one.Kind, $one.File, $one.Value)
}
Assert ($pinned.Count -ge 2) `
    "판을 못 박은 자리가 $($pinned.Count) 곳뿐이다 — static_assert 와 expected 상수 둘 다 있어야 한다"
Assert (@($pinned | Where-Object { $_.Kind -eq 'static_assert' }).Count -ge 1) `
    'IMGUI_VERSION_NUM 을 못 박는 static_assert 가 하나도 없다 — 판이 바뀌어도 빌드가 안 멈춘다'
Assert (@($pinned | Where-Object { $_.Kind -eq 'expected' }).Count -ge 1) `
    'expected_imgui_version_num 이 없다 — 런타임 감사가 대조할 기준이 없다'

$values = @($pinned | ForEach-Object { $_.Value } | Sort-Object -Unique)
Assert ($values.Count -eq 1) `
    "판 상수가 자리마다 다르다: $($values -join ', ') — 하나만 올리고 나머지를 두면 어댑터가 갈린다"
$pinnedVersion = [int]$values[0]

# static_assert 는 **컴파일되는 TU** 에 있어야 힘이 있다. 헤더에만 있고 아무도
# 포함하지 않으면 빌드를 멈추지 못한다.
$assertFiles = @($pinned | Where-Object { $_.Kind -eq 'static_assert' } | ForEach-Object { $_.File })
$compiled = @($assertFiles | Where-Object { $_ -like '*.cpp' })
Assert ($compiled.Count -ge 1) `
    "판을 못 박는 static_assert 가 .cpp 에 하나도 없다: [$($assertFiles -join ', ')]"
$projectText = Read-Source 'Editor/Editor.vcxproj'
foreach ($one in $compiled) {
    $leaf = Split-Path $one -Leaf
    Assert ($projectText.Contains($leaf)) `
        "판을 못 박는 $one 이 Editor.vcxproj 에 없다 — 컴파일되지 않으면 빌드를 못 멈춘다"
}
Write-Host ("  판 {0} · 못 박은 자리 {1} 곳 · 그중 컴파일되는 .cpp {2} 곳" -f `
    $pinnedVersion, $pinned.Count, $compiled.Count)

# ── ② legacy 잔재 재유입 ──────────────────────────────────────────────────
#
# 이미 죽은 것을 죽은 채로 둔다. 계획서가 이름을 댄 둘이다.
$browserStyleHits = @()
foreach ($file in $sources) {
    $text = [IO.File]::ReadAllText($file.FullName)
    if ($text -match 'ContentsBrowserStyle') {
        $browserStyleHits += $file.FullName.Substring($repo.Length + 1)
    }
}
Assert ($browserStyleHits.Count -eq 0) `
    "ContentsBrowserStyle 이 다시 들어왔다: [$($browserStyleHits -join ', ')] — 배치 빌더는 스타일로 갈리지 않는다"

# `no_move` 는 **중앙 뷰포트에만** 있어야 한다. 끌어 옮기면 중앙 노드가 비기
# 때문이고, 그 밖의 패널에 붙으면 자유 도킹을 막는 legacy 성질이 된다.
# 양쪽을 다 소스에서 뽑는다 — 선언자(central/panel/transient…)와 성질 목록.
$declarationFiles = @(Get-ChildItem -LiteralPath (Join-Path $editorRoot 'EditorWindow/Windows') -File -Filter *.h)
Assert ($declarationFiles.Count -gt 0) '창 선언 파일을 하나도 못 찾았다'
$noMoveOwners = @()
foreach ($file in $declarationFiles) {
    $text = [IO.File]::ReadAllText($file.FullName)
    # 선언 하나는 `<선언자>< ... >( ... )` 로 시작해 다음 선언자 앞까지다.
    $declMatches = @([regex]::Matches($text, '(?<kind>central|panel|transient|popup)\s*<'))
    for ($i = 0; $i -lt $declMatches.Count; $i++) {
        $start = $declMatches[$i].Index
        $end = if ($i + 1 -lt $declMatches.Count) { $declMatches[$i + 1].Index } else { $text.Length }
        $body = $text.Substring($start, $end - $start)
        if ($body -match 'window_trait::no_move') {
            $noMoveOwners += [pscustomobject]@{
                File = $file.Name; Kind = $declMatches[$i].Groups['kind'].Value }
        }
    }
}
foreach ($one in $noMoveOwners) {
    Write-Host ("  no_move 선언: {0,-28} {1}" -f $one.File, $one.Kind)
}
Assert ($noMoveOwners.Count -ge 1) `
    'no_move 를 실은 선언이 하나도 없다 — 중앙 뷰포트가 그 성질을 잃었거나 파서가 눈멀었다'
$strays = @($noMoveOwners | Where-Object { $_.Kind -ne 'central' })
Assert ($strays.Count -eq 0) `
    ("중앙이 아닌 선언이 no_move 를 실었다: [{0}] — 자유 도킹을 막는 legacy 성질이다" -f `
        (($strays | ForEach-Object { "$($_.File):$($_.Kind)" }) -join ', '))

# 기본 성질 쪽도 같다. `default_traits` 가 central 이 아닌 역할에 no_move 를 주면
# 선언을 아무리 봐도 안 보이는 자리에서 같은 일이 벌어진다.
$surface = Read-Source 'Editor/EditorWindow/EditorWindowSurface.h'
$defaultBlock = [regex]::Match($surface,
    'constexpr\s+window_trait\s+default_traits[\s\S]*?\n\s*\}')
Assert ($defaultBlock.Success) 'default_traits 를 못 찾았다 — 파서가 낡았다'
foreach ($case in [regex]::Matches($defaultBlock.Value,
    'case\s+window_role::(?<role>\w+):(?<body>[\s\S]*?)(?=case\s+window_role::|default:)')) {
    if ($case.Groups['body'].Value -match 'window_trait::no_move') {
        Assert ($case.Groups['role'].Value -eq 'central') `
            "default_traits 가 $($case.Groups['role'].Value) 역할에 no_move 를 준다"
    }
}
Write-Host "  legacy 잔재 — ContentsBrowserStyle 0 · no_move 는 central 전용"

# ── ③ 런타임이 말하는 판 ──────────────────────────────────────────────────
$dir = Join-Path $Work 'run'
if (Test-Path -LiteralPath $dir) { Remove-Item -LiteralPath $dir -Recurse -Force }
New-Item -ItemType Directory -Force -Path $dir | Out-Null
$scriptPath = Join-Path $dir 'script.txt'
$resultPath = Join-Path $dir 'result.jsonl'
Set-Content -LiteralPath $scriptPath -Encoding UTF8 -Value @(
    'window.resize 1400 900', 'wait 120', 'editor.dock', 'wait 20', 'quit')
$workspaceDir = Join-Path $dir 'workspace'
New-Item -ItemType Directory -Force -Path $workspaceDir | Out-Null
$env:CREATOR_EDITOR_WORKSPACE_DIR = $workspaceDir
$env:CREATOR_EDITOR_LEGACY_INI = Join-Path $workspaceDir 'none.ini'
try {
    $proc = Start-Process -FilePath $Exe -WorkingDirectory (Split-Path $Exe) -PassThru `
        -ArgumentList @('--script', ('"' + $scriptPath + '"'), '--result-format', 'jsonl',
                        '--result-file', ('"' + $resultPath + '"')) `
        -RedirectStandardOutput (Join-Path $dir 'out.txt') `
        -RedirectStandardError (Join-Path $dir 'err.txt')
    if (-not $proc.WaitForExit(600000)) { $proc.Kill(); throw '에디터가 제때 끝나지 않았다' }
}
finally {
    Remove-Item Env:CREATOR_EDITOR_WORKSPACE_DIR -ErrorAction SilentlyContinue
    Remove-Item Env:CREATOR_EDITOR_LEGACY_INI -ErrorAction SilentlyContinue
}
Assert (Test-Path -LiteralPath $resultPath) '결과 파일이 없다'
$dock = @(Get-Content -LiteralPath $resultPath -Encoding UTF8 | Where-Object { $_.Trim() } |
          ForEach-Object { $_ | ConvertFrom-Json } |
          Where-Object { $_.command -eq 'editor.dock' }) | Select-Object -First 1
Assert ($null -ne $dock -and $dock.status -eq 'succeeded') 'editor.dock 이 실패했다'

$have = @($dock.data.PSObject.Properties.Name)
foreach ($field in @('imguiVersionNum', 'versionKnown', 'imguiHeaderVersion',
                     'imguiRuntimeVersion', 'binaryMatchesHeader')) {
    Assert ($have -contains $field) "editor.dock 이 $field 를 싣지 않았다"
}
Assert ([int]$dock.data.imguiVersionNum -eq $pinnedVersion) `
    "런타임 판 $($dock.data.imguiVersionNum) 이 소스가 못 박은 $pinnedVersion 과 다르다"
Assert ([bool]$dock.data.versionKnown) `
    'versionKnown 이 거짓이다 — expected_imgui_version_num 이 실제 판과 어긋난다'
Assert (-not [string]::IsNullOrWhiteSpace($dock.data.imguiRuntimeVersion)) `
    '라이브러리가 말하는 판이 비어 있다 — 빈 값을 같다고 읽으면 이 축은 눈먼 초록이다'
Assert ($dock.data.imguiHeaderVersion -eq $dock.data.imguiRuntimeVersion) `
    ("헤더 판 '{0}' 과 라이브러리 판 '{1}' 이 다르다 — 설치본이 둘이고 섞였다" -f `
        $dock.data.imguiHeaderVersion, $dock.data.imguiRuntimeVersion)
Assert ([bool]$dock.data.binaryMatchesHeader) '감사가 헤더/라이브러리 불일치를 보고했다'

# 판 문자열과 판 번호를 **따로** 유도해 맞댄다. 같은 매크로에서 나온 두 값을
# 비교하면 동어반복이지만, 문자열은 사람이 읽는 표기이고 번호는 산수라 서로를
# 검산한다 — 1.92.8 → 1*10000 + 92*100 + 8*10.
$parts = @([string]$dock.data.imguiHeaderVersion -split '\.')
Assert ($parts.Count -eq 3) "판 문자열이 'major.minor.patch' 가 아니다: $($dock.data.imguiHeaderVersion)"
$derived = ([int]$parts[0] * 10000) + ([int]$parts[1] * 100) + ([int]$parts[2] * 10)
Assert ($derived -eq $pinnedVersion) `
    "판 문자열 $($dock.data.imguiHeaderVersion) 에서 유도한 $derived 이 못 박은 $pinnedVersion 과 다르다"
Write-Host ("  런타임 — 헤더 {0} · 라이브러리 {1} · 번호 {2} (문자열에서 유도한 값과 일치)" -f `
    $dock.data.imguiHeaderVersion, $dock.data.imguiRuntimeVersion, $dock.data.imguiVersionNum)

Write-Host ''
Write-Host ("PASS verify-imgui-adapter-canary — {0} checks, 판 고정 {1} 곳 · 에디터 1 회 기동" -f `
    $script:checks, $pinned.Count)
exit 0
