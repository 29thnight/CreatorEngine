[CmdletBinding()]
param(
    # 실패한 파일을 최대 몇 개까지 나열할지. 전수를 찍으면 로그가 묻힌다.
    [int]$ListLimit = 12
)

# SerializationPlan D3-b — 저작 텍스트 자산의 개행을 LF로 고정한다.
#
# ── 왜 LF인가 (실측 근거) ──
#
#   · **git index는 이미 LF다.** `.gitattributes`의 `* text=auto`가 커밋 시 정규화하고
#     있어서, 추적되는 저작 자산의 blob은 전부 LF다. 작업 트리만 CRLF/LF로 혼재한다.
#     즉 이 게이트가 요구하는 것은 저장소 내용 변경이 아니라 **작업 트리를 저장소와
#     일치시키는 것**이다.
#   · 그 혼재가 "저장할 때마다 git status는 더러워지는데 git diff는 비어 있다"의
#     원인이다(index는 LF라 diff가 비고, 작업 트리 바이트는 달라 status가 더럽다).
#   · **content-addressed VCS로 가면 정규화 계층이 사라진다.** `text=auto`는 Git 고유
#     기능이고, 내용 해시로 식별하는 시스템에서는 CRLF와 LF가 곧 다른 콘텐츠다.
#     LF는 플랫폼 중립이라 어느 VCS로 가도 안정적이다.
#   · ryml은 CRLF를 만나면 파싱 실패가 아니라 **프로세스를 abort한다**. LF로 통일하면
#     파싱 전 정규화 사본이 불필요해진다(D3-b).
#
# ── 이 게이트가 없으면 되돌아온다 ──
#
#   개행은 writer 한 곳만 바꿔도 다른 경로가 조용히 CRLF를 다시 쓴다. 실제로 이
#   저장소는 저작 텍스트를 **11곳**에서 쓴다. 그래서 결과를 자산에서 직접 재고,
#   writer의 모드도 함께 정적으로 확인한다.

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$repoRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))

# ★ **저작 자산은 `Assets/` 안에만 있지 않다.** 처음 이 게이트는 `Assets`만
#   스캔해서 "CRLF 0"을 찍었는데, 그 밖의 `ProjectSetting/*.asset` 3개가 여전히
#   CRLF였다. 게이트가 보지 않는 곳은 게이트가 지키는 곳이 아니다 — 범위를
#   좌게 잡은 초록은 거짓 통과다.
$scanRoots = @(
    (Join-Path $repoRoot 'Dynamic_CPP\Assets'),
    (Join-Path $repoRoot 'Dynamic_CPP\ProjectSetting')
)
$scanRoots = @($scanRoots | Where-Object { Test-Path -LiteralPath $_ -PathType Container })
if ($scanRoots.Count -eq 0) {
    "자산 루트가 하나도 없다"
    exit 1
}

# 저작 **텍스트** 자산만 본다. `.asset`은 확장자 하나가 두 포맷을 담으므로
# (재질=YAML, 모델=CEMA 바이너리) 내용으로 가른다.
$textExtensions = @('.creator', '.prefab', '.meta', '.shadermeta', '.volume', '.asset')

$failures = New-Object System.Collections.Generic.List[string]
$crlfFiles = New-Object System.Collections.Generic.List[string]
$checked = 0
$skippedBinary = 0

foreach ($file in ($scanRoots | ForEach-Object { Get-ChildItem -LiteralPath $_ -Recurse -File })) {
    if ($textExtensions -notcontains $file.Extension.ToLowerInvariant()) { continue }

    $bytes = [IO.File]::ReadAllBytes($file.FullName)
    if ($bytes.Length -lt 4) { continue }

    # CEMA 매직 = 모델 바이너리 캐시. 개행 판정 대상이 아니다.
    if ($bytes[0] -eq 0x43 -and $bytes[1] -eq 0x45 -and $bytes[2] -eq 0x4D -and $bytes[3] -eq 0x41) {
        $skippedBinary++
        continue
    }

    $checked++
    for ($i = 1; $i -lt $bytes.Length; $i++) {
        if ($bytes[$i] -eq 0x0A -and $bytes[$i - 1] -eq 0x0D) {
            $crlfFiles.Add($file.FullName.Substring($repoRoot.Length).TrimStart('\'))
            break
        }
    }
}

# ── 계약 1: 저작 텍스트 자산에 CRLF 0 ────────────────────────────────────────
if ($checked -eq 0) {
    # 0개를 검사하고 "CRLF 0"을 통과로 읽지 않는다.
    $failures.Add('검사한 텍스트 자산이 0개다 — 잴 것이 없다')
}
if ($crlfFiles.Count -gt 0) {
    $failures.Add("CRLF 자산 $($crlfFiles.Count)개 / 검사 $checked 개")
}

# ── 계약 2: .gitattributes가 저작 확장자의 개행을 **명시**한다 ───────────────
#
# `* text=auto`만 있으면 git이 내용을 보고 텍스트/바이너리를 추측한다. 지금은 모델
# `.asset`에 널 바이트가 많아 우연히 binary로 판정되지만, 추측에 기대는 상태다.
$attributesPath = Join-Path $repoRoot '.gitattributes'
if (-not (Test-Path -LiteralPath $attributesPath -PathType Leaf)) {
    $failures.Add('.gitattributes가 없다')
} else {
    $attributes = Get-Content -LiteralPath $attributesPath -Raw
    foreach ($ext in @('creator', 'prefab', 'meta', 'shadermeta', 'volume')) {
        if ($attributes -notmatch "(?m)^\s*\*\.$ext\s+text\s+eol=lf") {
            $failures.Add("*.$ext 의 eol=lf 규칙이 .gitattributes에 없다")
        }
    }
    if ($attributes -notmatch '(?m)^\s*\S*Models\S*\.asset\s+binary') {
        $failures.Add('모델 .asset을 binary로 명시하는 규칙이 없다 (확장자 하나가 두 포맷을 담는다)')
    }
    # ProjectSetting은 `Assets/` 밖이라 앞 규칙에 걸리지 않는다. 이 규칙이 없으면
    # `* text=auto` + `core.autocrlf=true`가 **체크아웃마다 CRLF로 되돌린다** —
    # 작업 트리를 고쳐도 다음 clone에서 부활하므로 결과만 재서는 못 잡는다.
    if ($attributes -notmatch '(?m)^\s*\S*ProjectSetting\S*\.asset\s+text\s+eol=lf') {
        $failures.Add('ProjectSetting/*.asset 의 eol=lf 규칙이 .gitattributes에 없다')
    }
}

# ── 계약 3: 저작 writer가 텍스트 모드를 쓰지 않는다 ──────────────────────────
#
# Windows의 텍스트 모드 ofstream은 `\n`을 `\r\n`으로 바꾼다. 결과만 재면 "지금은
# 깨끗하지만 다음 저장에서 되돌아오는" 상태를 통과시킨다.
$writerFiles = @(
    'Engine\SceneRuntime\PrefabUtility.cpp',
    'Engine\SceneRuntime\SceneManager.cpp',
    'Editor\EngineEntry\EditorAssetDatabase.cpp',
    'Editor\EngineGUIWindow\MenuBarWindow.cpp',
    'Editor\EngineGUIWindow\InspectorWindow.cpp'
)
$textModeWriters = New-Object System.Collections.Generic.List[string]
foreach ($relative in $writerFiles) {
    $full = Join-Path $repoRoot $relative
    if (-not (Test-Path -LiteralPath $full -PathType Leaf)) { continue }
    # CP949 파일이 섞여 있으므로 바이트로 읽어 Latin1로 훑는다(내용을 고치지 않는다).
    $text = [Text.Encoding]::GetEncoding(28591).GetString([IO.File]::ReadAllBytes($full))

    # ★ 한 줄만 보면 오탐한다. 실제로 이 게이트를 처음 돌렸을 때 둘을 잘못 잡았다:
    #   · 멀티라인 선언 — `ofstream output(path,` 다음 줄에 `ios::binary`가 있다
    #   · 변수 모드 — `ofstream output(path, mode)`이고 mode가 위에서 binary로 정해진다
    #   그래서 선언문을 **세미콜론까지 이어붙여** 판정하고, 변수 모드는 그 변수가
    #   binary로 정의됐는지 파일 안에서 확인한다.
    $modeVarsAreBinary = ($text -match 'openmode\s+\w+\s*=[^;]*ios::binary')

    $lines = $text -split "`r?`n"
    for ($i = 0; $i -lt $lines.Count; $i++) {
        if ($lines[$i] -notmatch 'std::ofstream') { continue }
        # 로그·덤프 경로는 저작 자산이 아니다.
        if ($lines[$i] -match 'dump|log|crash|report') { continue }

        # 선언문 전체를 모은다(최대 3줄).
        $statement = $lines[$i]
        $j = $i
        while ($statement -notmatch ';' -and ($j - $i) -lt 3 -and ($j + 1) -lt $lines.Count) {
            $j++
            $statement += ' ' + $lines[$j].Trim()
        }

        if ($statement -match 'ios::binary') { continue }
        # `, mode)` 형태이고 그 모드가 binary로 정의돼 있으면 통과다.
        if ($modeVarsAreBinary -and $statement -match ',\s*\w*[Mm]ode\s*\)') { continue }

        $textModeWriters.Add("$relative : $($statement.Trim())")
    }
}
if ($textModeWriters.Count -gt 0) {
    $failures.Add("텍스트 모드 ofstream $($textModeWriters.Count)곳 — 다음 저장에서 CRLF가 되돌아온다")
}

# ── 보고 ─────────────────────────────────────────────────────────────────────
"checkedTextAssets=$checked skippedBinary=$skippedBinary crlfAssets=$($crlfFiles.Count) textModeWriters=$($textModeWriters.Count)"
if ($crlfFiles.Count -gt 0) {
    'CRLF 자산(일부):'
    $crlfFiles | Select-Object -First $ListLimit | ForEach-Object { "  $_" }
    if ($crlfFiles.Count -gt $ListLimit) { "  ... 외 $($crlfFiles.Count - $ListLimit)개" }
}
if ($textModeWriters.Count -gt 0) {
    '텍스트 모드 writer:'
    $textModeWriters | Select-Object -First $ListLimit | ForEach-Object { "  $_" }
}

if ($failures.Count -gt 0) {
    ''
    '실패:'
    $failures | ForEach-Object { "  - $_" }
    exit 1
}

'전체 통과 — 저작 텍스트 자산이 LF이고, .gitattributes와 writer가 그것을 고정한다'
exit 0
