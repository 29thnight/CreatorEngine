param(
    [string]$Exe = (Join-Path $PSScriptRoot "..\..\Bin\x64-Debug\Editor\CreatorEditor.exe"),
    [string]$Work = $env:TEMP,
    [int]$ExpectedMaps = 6,
    [int]$ExpectedActions = 26,
    [int]$TimeoutSeconds = 180
)

# SerializationPlan D4 — InputMap은 YAML `.inputmap`만 소비한다. 실제 코퍼스의
# 의미 수치와 무변이를 함께 확인해 빈 집합·부분 로드·검증 중 재저장을 막는다.

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
$inputRoot = Join-Path $repoRoot 'Dynamic_CPP\Assets\InputMap'
$maps = @(Get-ChildItem -LiteralPath $inputRoot -Filter '*.inputmap' -File | Sort-Object Name)
$legacyJson = @(Get-ChildItem -LiteralPath $inputRoot -Filter '*.json' -File)
if ($maps.Count -ne $ExpectedMaps) {
    throw "Expected $ExpectedMaps .inputmap files, found $($maps.Count)"
}
if ($legacyJson.Count -ne 0) {
    throw "Legacy InputMap JSON remains: $($legacyJson[0].FullName)"
}

$runtimeText = Get-Content -LiteralPath `
    (Join-Path $repoRoot 'Engine\SceneRuntime\InputActionManager.cpp') -Raw
$runtimeHeader = Get-Content -LiteralPath `
    (Join-Path $repoRoot 'Engine\SceneRuntime\InputActionManager.h') -Raw
if (($runtimeText + $runtimeHeader) -match 'nlohmann|\.json|SerializeMap|DeSerializeMap') {
    throw 'InputActionManager still exposes the retired JSON contract'
}

$before = @{}
foreach ($map in $maps) {
    $text = Get-Content -LiteralPath $map.FullName -Raw
    if ($text -notmatch '(?m)^schemaVersion:\s*1\s*$' -or
        $text -notmatch '(?m)^actions:\s*$' -or $text.TrimStart().StartsWith('{')) {
        throw "InputMap is not canonical YAML: $($map.FullName)"
    }
    $before[$map.FullName] = (Get-FileHash -LiteralPath $map.FullName -Algorithm SHA256).Hash
}

if (-not (Test-Path -LiteralPath $Exe -PathType Leaf)) {
    throw "Editor executable is missing: $Exe"
}
$Exe = (Resolve-Path -LiteralPath $Exe).Path
$run = Join-Path $Work ('CE_D4InputMap_' + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $run -Force | Out-Null
$scenario = Join-Path $run 'commands.txt'
$stdout = Join-Path $run 'stdout.txt'
$stderr = Join-Path $run 'stderr.txt'
@('inputmap.corpus.probe', 'quit') | Set-Content -LiteralPath $scenario -Encoding UTF8

try {
    $process = Start-Process -FilePath $Exe -ArgumentList @('--commandlet-script', $scenario) `
        -WorkingDirectory $repoRoot -WindowStyle Hidden `
        -RedirectStandardOutput $stdout -RedirectStandardError $stderr -PassThru
    $process.WaitForExit($TimeoutSeconds * 1000) | Out-Null
    if (-not $process.HasExited) {
        $process.Kill()
        throw "InputMap corpus probe timed out: $run"
    }

    $text = if (Test-Path -LiteralPath $stdout) {
        Get-Content -LiteralPath $stdout -Raw
    } else { '' }
    $summary = [regex]::Match($text,
        '\[inputmap\.corpus\.probe\] maps=(\d+) actions=(\d+) keys=(\d+) keyboard=(\d+) gamepad=(\d+) buttons=(\d+) values=(\d+) invalid=(\d+) selfcheck=(\w+)')
    if (-not $summary.Success) {
        throw "InputMap corpus summary is missing; binary may be stale: $run"
    }

    $loadedMaps = [int]$summary.Groups[1].Value
    $actions = [int]$summary.Groups[2].Value
    $keys = [int]$summary.Groups[3].Value
    $keyboard = [int]$summary.Groups[4].Value
    $gamepad = [int]$summary.Groups[5].Value
    $buttons = [int]$summary.Groups[6].Value
    $values = [int]$summary.Groups[7].Value
    $invalid = [int]$summary.Groups[8].Value
    $selfcheck = $summary.Groups[9].Value
    if ($loadedMaps -ne $ExpectedMaps -or $actions -ne $ExpectedActions -or
        $keys -ne 104 -or $keyboard -ne 8 -or $gamepad -ne 18 -or
        $buttons -ne 22 -or $values -ne 4 -or $invalid -ne 0 -or
        $selfcheck -ne 'pass') {
        throw "InputMap corpus contract failed: $($summary.Value)"
    }
    if ($process.ExitCode -ne 0) {
        throw "Editor exited with code $($process.ExitCode): $run"
    }

    foreach ($map in $maps) {
        $after = (Get-FileHash -LiteralPath $map.FullName -Algorithm SHA256).Hash
        if ($after -ne $before[$map.FullName]) {
            throw "InputMap probe mutated source: $($map.FullName)"
        }
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

    "inputmap YAML corpus: PASS (maps=$loadedMaps actions=$actions keys=$keys keyboard=$keyboard gamepad=$gamepad buttons=$buttons values=$values invalid=$invalid sourceMutations=0)"
}
finally {
    if (Test-Path -LiteralPath $run) {
        $absoluteRun = [IO.Path]::GetFullPath($run)
        $absoluteWork = [IO.Path]::GetFullPath($Work)
        if (-not $absoluteRun.StartsWith($absoluteWork, [StringComparison]::OrdinalIgnoreCase) -or
            -not (Split-Path $absoluteRun -Leaf).StartsWith(
                'CE_D4InputMap_', [StringComparison]::OrdinalIgnoreCase)) {
            throw "Refusing to remove unverified temp directory: $absoluteRun"
        }
        Remove-Item -LiteralPath $absoluteRun -Recurse -Force
    }
}
