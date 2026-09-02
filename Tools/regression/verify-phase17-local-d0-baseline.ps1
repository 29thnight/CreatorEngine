[CmdletBinding()]
param(
    [string]$Exe = (Join-Path $PSScriptRoot '..\..\Bin\x64-Release\Editor\CreatorEditor.exe'),
    [string]$SceneRoot = (Join-Path $PSScriptRoot '..\..\Dynamic_CPP\Assets\Scenes'),
    [string[]]$Scenes = @(),
    [string]$Prefab = 'NestedProbeParent',
    [string]$Work = $env:TEMP,
    [ValidateRange(1, 100)]
    [int]$SceneIterations = 5,
    [ValidateRange(1, 100)]
    [int]$PrefabIterations = 10,
    [ValidateRange(30, 1800)]
    [int]$TimeoutSeconds = 300
)

# PHASE 17 D0 local acceptance gate.
#
# The canonical D0 gate intentionally names Test1.creator because its published
# numbers use that exact workload.  A local checkout without Test1 must not
# silently rename a smaller scene to Test1 or rewrite the historical baseline.
# This gate instead selects the two largest current local scenes, records their
# sizes and hashes, and verifies the same Release instrumentation contract.

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$repoRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
$failures = [Collections.Generic.List[string]]::new()

function Add-Failure([string]$Message) {
    $script:failures.Add($Message)
}

function Resolve-File([string]$Path, [string]$Label) {
    if ([string]::IsNullOrWhiteSpace($Path) -or
        -not (Test-Path -LiteralPath $Path -PathType Leaf)) {
        Add-Failure "$Label 파일이 없다: $Path"
        return ''
    }
    return (Resolve-Path -LiteralPath $Path).Path
}

$Exe = Resolve-File -Path $Exe -Label 'Release Editor'
$resolvedScenes = [Collections.Generic.List[string]]::new()
if (@($Scenes).Count -eq 0) {
    if (-not (Test-Path -LiteralPath $SceneRoot -PathType Container)) {
        Add-Failure "로컬 scene root가 없다: $SceneRoot"
    } else {
        $candidates = @(Get-ChildItem -LiteralPath $SceneRoot -File -Filter '*.creator' |
            Where-Object { $_.Length -gt 0 } |
            Sort-Object -Property @{ Expression = 'Length'; Descending = $true },
                @{ Expression = 'FullName'; Descending = $false })
        foreach ($candidate in @($candidates | Select-Object -First 2)) {
            $resolvedScenes.Add($candidate.FullName)
        }
    }
} else {
    foreach ($scene in @($Scenes)) {
        $resolved = Resolve-File -Path $scene -Label 'scene'
        if (-not [string]::IsNullOrWhiteSpace($resolved) -and
            -not $resolvedScenes.Contains($resolved)) {
            $resolvedScenes.Add($resolved)
        }
    }
}

if ($resolvedScenes.Count -ne 2) {
    Add-Failure "서로 다른 로컬 scene 2개가 필요하다: selected=$($resolvedScenes.Count)"
}

if ($failures.Count -gt 0) {
    $failures | ForEach-Object { "실패: $_" }
    exit 1
}

$run = Join-Path $Work ('CE_Phase17LocalD0_' + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $run -Force | Out-Null
$scenario = Join-Path $run 'commands.txt'
$stdout = Join-Path $run 'stdout.txt'
$stderr = Join-Path $run 'stderr.txt'

$commands = [Collections.Generic.List[string]]::new()
$commands.Add('serialize.bench boot')
foreach ($scene in $resolvedScenes) {
    $commands.Add("serialize.bench scene $($scene.Replace('\', '/')) $SceneIterations")
}
$commands.Add("serialize.bench prefab $Prefab $PrefabIterations")
$commands.Add('quit')
$commands | Set-Content -LiteralPath $scenario -Encoding utf8NoBOM

$process = Start-Process -FilePath $Exe -ArgumentList @('--script', $scenario) `
    -WorkingDirectory $repoRoot -WindowStyle Hidden -PassThru `
    -RedirectStandardOutput $stdout -RedirectStandardError $stderr
if (-not $process.WaitForExit($TimeoutSeconds * 1000)) {
    $process.Kill()
    Add-Failure "Release 계측 시간 초과: $run"
} else {
    # Redirected streams can finish after the process handle is signaled.
    $process.WaitForExit()
    if ($process.ExitCode -ne 0) {
        Add-Failure "Release Editor exit code=$($process.ExitCode): $run"
    }
}

$text = if (Test-Path -LiteralPath $stdout) {
    Get-Content -LiteralPath $stdout -Raw
} else { '' }
$errorLines = @(if (Test-Path -LiteralPath $stderr) {
    Get-Content -LiteralPath $stderr | Where-Object {
        -not [string]::IsNullOrWhiteSpace($_) -and
        $_ -ne 'Mesh::GenerateLODs: MeshOptimizer failed to generate LODs.'
    }
})
if ($errorLines.Count -gt 0) {
    Add-Failure "예상 밖 stderr $($errorLines.Count)줄: $($errorLines[0])"
}

$configs = @([regex]::Matches($text, '\[serialize\.bench\] config=(\w+)') |
    ForEach-Object { $_.Groups[1].Value })
if ($configs.Count -eq 0) {
    Add-Failure 'serialize.bench config 라인이 없다'
} elseif (@($configs | Where-Object { $_ -ne 'Release' }).Count -gt 0) {
    Add-Failure "Release가 아닌 구성으로 측정됐다: $($configs -join ',')"
}

$selfchecks = @([regex]::Matches($text,
    '\[serialize\.bench\] mode=(\w+) selfcheck=(\w+)(?: reason=([\w-]+))?') |
    ForEach-Object {
        [pscustomobject]@{
            Mode = $_.Groups[1].Value
            Result = $_.Groups[2].Value
            Reason = $_.Groups[3].Value
        }
    })
$expectedSelfchecks = 2 + $resolvedScenes.Count # boot + scenes + prefab
if ($selfchecks.Count -ne $expectedSelfchecks) {
    Add-Failure "selfcheck가 $($selfchecks.Count)개다: expected=$expectedSelfchecks"
}
foreach ($selfcheck in $selfchecks) {
    if ($selfcheck.Result -ne 'pass') {
        Add-Failure "selfcheck 실패: mode=$($selfcheck.Mode) reason=$($selfcheck.Reason)"
    }
}

$boot = [regex]::Match($text,
    '\[serialize\.bench\] mode=boot stage=AssetCatalog totalMs=([\d.]+) parsedMeta=(\d+)')
if (-not $boot.Success) {
    Add-Failure 'boot catalog 계측 라인이 없다'
} elseif ([int]$boot.Groups[2].Value -le 0) {
    Add-Failure 'boot catalog가 meta를 하나도 파싱하지 않았다'
}

$sceneBlocks = @([regex]::Matches($text,
    '\[serialize\.bench\] mode=scene target=(?<target>\S+) iterations=(?<iters>\d+) loaded=(?<loaded>\d+) warmup=\d+'))
if ($sceneBlocks.Count -ne $resolvedScenes.Count) {
    Add-Failure "scene 계측 블록이 $($sceneBlocks.Count)개다: expected=$($resolvedScenes.Count)"
}
$ratios = @([regex]::Matches($text,
    '\[serialize\.bench\] mode=scene rootUs=(?<root>[\d.]+) childSumUs=(?<child>[\d.]+) unattributedUs=(?<un>-?[\d.]+) childRatio=(?<ratio>[\d.]+)'))
if ($ratios.Count -ne $resolvedScenes.Count) {
    Add-Failure "scene 단계 분해가 $($ratios.Count)개다: expected=$($resolvedScenes.Count)"
}
foreach ($ratio in $ratios) {
    $rootUs = [double]$ratio.Groups['root'].Value
    $childUs = [double]$ratio.Groups['child'].Value
    if ($rootUs -le 0) { Add-Failure 'scene rootUs가 0 이하다' }
    if ($childUs -gt $rootUs) { Add-Failure 'scene childSumUs가 rootUs를 넘는다' }
}

"phase17-local-d0 canonicalComparable=false exe=$Exe"
foreach ($scene in $resolvedScenes) {
    $item = Get-Item -LiteralPath $scene
    $hash = (Get-FileHash -LiteralPath $scene -Algorithm SHA256).Hash.ToLowerInvariant()
    "scene=$scene bytes=$($item.Length) sha256=$hash"
}
"prefab=$Prefab sceneIterations=$SceneIterations prefabIterations=$PrefabIterations"
"selfchecks=$($selfchecks.Count)/$expectedSelfchecks output=$run"
@([regex]::Matches($text, '\[serialize\.bench\][^\r\n]*')) |
    ForEach-Object { $_.Value }

if ($failures.Count -gt 0) {
    ''
    '실패:'
    $failures | ForEach-Object { "  - $_" }
    exit 1
}

'전체 통과 — 현재 로컬 scene 2개로 Release D0 계측 계약을 복구했다 (Test1 정본 수치와 비교 불가)'
exit 0
