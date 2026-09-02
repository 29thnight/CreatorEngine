param(
    [string]$Exe = (Join-Path $PSScriptRoot "..\..\Bin\x64-Debug\Editor\CreatorEditor.exe"),
    [string]$Work = $env:TEMP,
    [int]$TimeoutSeconds = 180
)

# SerializationPlan D4 — Animator controller graph는 씬 reflection YAML만 쓴다.
# 별도 JSON reader/writer가 다시 생기지 않는지와, 현재 그래프의 포인터 연결까지
# YAML 왕복 뒤 복원되는지를 함께 검증한다.

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
$animatorSource = Get-Content -LiteralPath `
    (Join-Path $repoRoot 'Engine\SceneRuntime\Animator.cpp') -Raw
$animatorHeader = Get-Content -LiteralPath `
    (Join-Path $repoRoot 'Engine\SceneRuntime\Animator.h') -Raw
$editorDatabase = Get-Content -LiteralPath `
    (Join-Path $repoRoot 'Editor\EngineEntry\EditorAssetDatabase.cpp') -Raw
$combined = $animatorSource + "`n" + $animatorHeader + "`n" + $editorDatabase
if ($combined -match 'nlohmann|SerializeControllers|DeserializeControllers|ImportLegacyControllerJson|WriteAnimatorController|AnimatorControllerWriter') {
    throw 'Animator JSON compatibility or writer path still exists'
}

if (-not (Test-Path -LiteralPath $Exe -PathType Leaf)) {
    throw "Editor executable is missing: $Exe"
}
$Exe = (Resolve-Path -LiteralPath $Exe).Path

$run = Join-Path $Work ('CE_D4AnimatorScene_' + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $run -Force | Out-Null
$scenario = Join-Path $run 'commands.txt'
$stdout = Join-Path $run 'stdout.txt'
$stderr = Join-Path $run 'stderr.txt'
@('animator.scene.probe', 'quit') | Set-Content -LiteralPath $scenario -Encoding UTF8

try {
    $process = Start-Process -FilePath $Exe -ArgumentList @('--script', $scenario) `
        -WorkingDirectory $repoRoot -WindowStyle Hidden `
        -RedirectStandardOutput $stdout -RedirectStandardError $stderr -PassThru
    $process.WaitForExit($TimeoutSeconds * 1000) | Out-Null
    if (-not $process.HasExited) {
        $process.Kill()
        throw "Animator scene probe timed out: $run"
    }

    $text = if (Test-Path -LiteralPath $stdout) {
        Get-Content -LiteralPath $stdout -Raw
    } else { '' }
    $summary = [regex]::Match($text,
        '\[animator\.scene\.probe\] controllers=(\d+) parameters=(\d+) states=(\d+) transitions=(\d+) conditions=(\d+) yamlBytes=(\d+) stable=(\d+) links=(\d+) selfcheck=(\w+)')
    if (-not $summary.Success) {
        throw "Animator scene summary is missing; binary may be stale: $run"
    }

    $controllers = [int]$summary.Groups[1].Value
    $parameters = [int]$summary.Groups[2].Value
    $states = [int]$summary.Groups[3].Value
    $transitions = [int]$summary.Groups[4].Value
    $conditions = [int]$summary.Groups[5].Value
    $yamlBytes = [int]$summary.Groups[6].Value
    $stable = [int]$summary.Groups[7].Value
    $links = [int]$summary.Groups[8].Value
    $selfcheck = $summary.Groups[9].Value

    if ($controllers -ne 1 -or $parameters -ne 1 -or $states -ne 3 -or
        $transitions -ne 1 -or $conditions -ne 1 -or $yamlBytes -le 0 -or
        $stable -ne 1 -or $links -ne 1 -or $selfcheck -ne 'pass') {
        throw "Animator scene contract failed: $($summary.Value)"
    }
    if ($process.ExitCode -ne 0) {
        throw "Editor exited with code $($process.ExitCode): $run"
    }

    $errorLines = if (Test-Path -LiteralPath $stderr) {
        @(Get-Content -LiteralPath $stderr)
    } else { @() }
    $knownLodWarning = 'Mesh::GenerateLODs: MeshOptimizer failed to generate LODs.'
    $unexpected = @($errorLines | Where-Object {
        -not [string]::IsNullOrWhiteSpace($_) -and $_ -ne $knownLodWarning
    })
    if ($unexpected.Count -gt 0) {
        throw "Unexpected stderr: $($unexpected[0])"
    }

    "animator scene single truth: PASS (controllers=$controllers parameters=$parameters states=$states transitions=$transitions conditions=$conditions yamlBytes=$yamlBytes stable=$stable links=$links)"
}
finally {
    if (Test-Path -LiteralPath $run) {
        $absoluteRun = [IO.Path]::GetFullPath($run)
        $absoluteWork = [IO.Path]::GetFullPath($Work)
        if (-not $absoluteRun.StartsWith($absoluteWork, [StringComparison]::OrdinalIgnoreCase) -or
            -not (Split-Path $absoluteRun -Leaf).StartsWith(
                'CE_D4AnimatorScene_', [StringComparison]::OrdinalIgnoreCase)) {
            throw "Refusing to remove unverified temp directory: $absoluteRun"
        }
        Remove-Item -LiteralPath $absoluteRun -Recurse -Force
    }
}
