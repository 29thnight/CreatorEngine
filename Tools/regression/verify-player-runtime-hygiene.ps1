[CmdletBinding()]
param(
    [string]$PlayerExe = '',
    [string]$EditorExe = ''
)

# SerializationPlan D1(Y-3) — Player가 에디터 저작 인프라를 끌고 들어가지 않는지.
#
# ★ 이 게이트는 **이미 참인 것을 고정한다.** 계획서 D1이 지목했던 두 결함
#   (`ScanAndGenerateMissingMeta`의 무가드 `.meta` 생성 스캔, Player 프로세스의 efsw
#   워처)은 EngineLayerSeparation E 트랙이 이미 밀어냈다 — 심볼 자체가 없고 efsw는
#   Editor 프로젝트에만 있다(2026-08-30 실측, §1.7 ①). 고칠 것이 없다는 것과
#   되돌아오지 않는다는 것은 다른 명제이므로, 그 상태를 계약으로 못 박는다.
#
# ★ 바이너리 검사에는 **대조군**이 있다. "Player.exe에 efsw 문자열 0"만 단정하면
#   경로가 틀려 빈 파일을 읽어도 통과한다. 같은 방법으로 CreatorEditor.exe를 재서
#   거기서는 반드시 검출되어야 한다고 함께 단정한다 — 검사가 눈을 뜨고 있음을
#   검사 자신이 증명하게 하는 장치다.

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$repoRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))

function Resolve-FirstExisting {
    param([string[]]$Candidates)
    foreach ($c in $Candidates) {
        if (Test-Path -LiteralPath $c -PathType Leaf) { return (Resolve-Path -LiteralPath $c).Path }
    }
    return ''
}

if ([string]::IsNullOrWhiteSpace($PlayerExe)) {
    $PlayerExe = Resolve-FirstExisting @(
        (Join-Path $repoRoot 'Bin\x64-Debug\Player\Player.exe'),
        (Join-Path $repoRoot 'Bin\x64-Release\Player\Player.exe'))
} else {
    # 인자로 받은 경로는 상대일 수 있다. 절대화하지 않으면 아래 보고에서
    # 저장소 루트 접두를 잘라낼 때 터진다(변이 실험에서 실제로 크래시했다).
    $PlayerExe = Resolve-FirstExisting @($PlayerExe)
}
if ([string]::IsNullOrWhiteSpace($EditorExe)) {
    $EditorExe = Resolve-FirstExisting @(
        (Join-Path $repoRoot 'Bin\x64-Debug\Editor\CreatorEditor.exe'),
        (Join-Path $repoRoot 'Bin\x64-Release\Editor\CreatorEditor.exe'))
} else {
    $EditorExe = Resolve-FirstExisting @($EditorExe)
}

$failures = New-Object System.Collections.Generic.List[string]

# ── 계약 1: 엔진 계층 소스에 파일 워처가 없다 ────────────────────────────────
# efsw는 에디터 저작 도구다. Engine/ 아래에 참조가 생기면 Player가 링크하게 된다.
$engineEfsw = @(Get-ChildItem -Path (Join-Path $repoRoot 'Engine') -Recurse -File `
        -Include '*.h', '*.hpp', '*.cpp' -ErrorAction SilentlyContinue |
    Select-String -Pattern 'efsw' -SimpleMatch -CaseSensitive:$false -List)
if ($engineEfsw.Count -gt 0) {
    $failures.Add("Engine/ 트리에 efsw 참조 $($engineEfsw.Count)건: " +
        (($engineEfsw | ForEach-Object { $_.Path.Substring($repoRoot.Length) }) -join ', '))
}

# ── 계약 2: Player가 에디터 진입 계층을 링크하지 않는다 ──────────────────────
$playerProject = Join-Path $repoRoot 'Player\Player.vcxproj'
if (-not (Test-Path -LiteralPath $playerProject -PathType Leaf)) {
    $failures.Add("Player.vcxproj가 없다: $playerProject")
} else {
    $projectText = Get-Content -LiteralPath $playerProject -Raw

    # ★ 반드시 `ProjectReference`의 Include 값만 본다.
    #
    #   처음에는 파일 전체에서 프로젝트 이름을 부분 문자열로 찾았고, 그래서
    #   `AdditionalIncludeDirectories`에 있는 `Editor\EngineEntry\`·`Editor\ImGuiHelper\`를
    #   "링크한다"고 오판해 거짓 실패를 냈다. 링크 그래프를 재려면 링크 그래프를
    #   읽어야 한다 — 경계 없는 문자열 비교는 엉뚱한 등장을 잡는다.
    #
    #   include 경로에 에디터 헤더가 열려 있는 것은 별개의 사안이고
    #   EngineLayerSeparation E 트랙의 소유다. 여기서 판정하지 않는다.
    $references = @([regex]::Matches($projectText, '<ProjectReference\s+Include="([^"]+)"') |
        ForEach-Object { $_.Groups[1].Value })
    if ($references.Count -eq 0) {
        $failures.Add('Player.vcxproj에서 ProjectReference를 하나도 읽지 못했다 — 검사가 눈을 감았다')
    }
    foreach ($forbidden in @('EngineEntry', 'EngineGUIWindow', 'ImGuiHelper', 'EditorRender')) {
        $hit = @($references | Where-Object {
            [IO.Path]::GetFileNameWithoutExtension($_) -eq $forbidden })
        if ($hit.Count -gt 0) {
            $failures.Add("Player.vcxproj가 에디터 프로젝트를 링크한다: $forbidden")
        }
    }
}

# ── 계약 3: `.meta` 생성 스캔이 되살아나지 않았다 ────────────────────────────
# 이름이 돌아오면 Player 부팅이 다시 자산 트리를 쓰게 된다.
$scanSymbol = @(Get-ChildItem -Path (Join-Path $repoRoot 'Engine'), (Join-Path $repoRoot 'Player') `
        -Recurse -File -Include '*.h', '*.hpp', '*.cpp' -ErrorAction SilentlyContinue |
    Select-String -Pattern 'ScanAndGenerateMissingMeta' -SimpleMatch -List)
if ($scanSymbol.Count -gt 0) {
    $failures.Add("Engine/Player에 ScanAndGenerateMissingMeta가 되살아났다: " +
        (($scanSymbol | ForEach-Object { $_.Path.Substring($repoRoot.Length) }) -join ', '))
}

# ── 계약 4: 산출 바이너리 대조 ───────────────────────────────────────────────
function Test-BinaryContains {
    param([Parameter(Mandatory)][string]$Path, [Parameter(Mandatory)][string]$Needle)
    $bytes = [IO.File]::ReadAllBytes($Path)
    # Latin1은 바이트↔문자 1:1이라 임의 바이너리를 손실 없이 훑을 수 있다.
    $text = [Text.Encoding]::GetEncoding(28591).GetString($bytes)
    return $text.IndexOf($Needle, [StringComparison]::OrdinalIgnoreCase) -ge 0
}

$binaryChecked = $false
if ([string]::IsNullOrWhiteSpace($PlayerExe)) {
    $failures.Add('Player.exe를 찾지 못해 바이너리 대조를 하지 못했다 (Debug/Release 중 하나를 빌드할 것)')
} elseif ([string]::IsNullOrWhiteSpace($EditorExe)) {
    # 대조군 없이 "0건"만 보고하면 그 0이 무엇을 뜻하는지 알 수 없다.
    $failures.Add('대조군 CreatorEditor.exe가 없어 바이너리 검사의 판별력을 증명할 수 없다')
} else {
    $playerHasEfsw = Test-BinaryContains -Path $PlayerExe -Needle 'efsw'
    $editorHasEfsw = Test-BinaryContains -Path $EditorExe -Needle 'efsw'
    $binaryChecked = $true

    if ($playerHasEfsw) {
        $failures.Add("Player.exe가 efsw를 참조한다: $PlayerExe")
    }
    if (-not $editorHasEfsw) {
        # 대조군이 반응하지 않으면 검사 자체가 죽은 것이다.
        $failures.Add("대조군 CreatorEditor.exe에서 efsw를 못 찾았다 — 이 검사는 판별력이 없다: $EditorExe")
    }
}

# ── 계약 5: Player 배포 디렉터리에 워처 DLL이 없다 ───────────────────────────
$playerDirs = @(
    (Join-Path $repoRoot 'Bin\x64-Debug\Player'),
    (Join-Path $repoRoot 'Bin\x64-Release\Player')
) | Where-Object { Test-Path -LiteralPath $_ -PathType Container }
foreach ($dir in $playerDirs) {
    $stray = @(Get-ChildItem -Path $dir -Filter 'efsw*.dll' -File -ErrorAction SilentlyContinue)
    if ($stray.Count -gt 0) {
        $failures.Add("Player 배포에 efsw DLL이 있다: $($stray[0].FullName)")
    }
}

# ── 보고 ─────────────────────────────────────────────────────────────────────
function Format-RepoRelative {
    param([string]$Path)
    if ([string]::IsNullOrWhiteSpace($Path)) { return '(없음)' }
    if ($Path.StartsWith($repoRoot, [StringComparison]::OrdinalIgnoreCase)) {
        return $Path.Substring($repoRoot.Length).TrimStart('\', '/')
    }
    return $Path   # 저장소 밖 경로는 그대로 보여 준다
}

"playerExe=$(Format-RepoRelative $PlayerExe)"
"editorExe=$(Format-RepoRelative $EditorExe)"
"engineEfswRefs=$($engineEfsw.Count) scanSymbolRefs=$($scanSymbol.Count) binaryCompared=$binaryChecked playerDirsChecked=$($playerDirs.Count)"

if ($failures.Count -gt 0) {
    '실패:'
    $failures | ForEach-Object { "  - $_" }
    exit 1
}

'전체 통과 — Player는 파일 워처도 .meta 생성 스캔도 끌고 들어가지 않는다 (대조군으로 판별력 확인)'
exit 0
