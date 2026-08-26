[CmdletBinding()]
param(
    [string]$AssetsRoot = "",
    [string]$EditorExe = "",
    [string]$OutDir = "",
    [int]$TimeoutSec = 900
)

$ErrorActionPreference = "Stop"
$repoRoot = [IO.Path]::GetFullPath(
    (Split-Path (Split-Path $PSScriptRoot -Parent) -Parent))

if ([string]::IsNullOrWhiteSpace($AssetsRoot)) {
    $AssetsRoot = Join-Path $repoRoot "Dynamic_CPP\Assets"
}
if ([string]::IsNullOrWhiteSpace($EditorExe)) {
    $EditorExe = Join-Path $repoRoot "Bin\x64-Debug\Editor\CreatorEditor.exe"
}
if ([string]::IsNullOrWhiteSpace($OutDir)) {
    $OutDir = Join-Path $repoRoot "Build\model-asset-rebuild"
}

$AssetsRoot = [IO.Path]::GetFullPath($AssetsRoot)
$EditorExe = [IO.Path]::GetFullPath($EditorExe)
$OutDir = [IO.Path]::GetFullPath($OutDir)
$modelRoot = [IO.Path]::GetFullPath((Join-Path $AssetsRoot "Models"))

if (-not (Test-Path -LiteralPath $AssetsRoot -PathType Container)) {
    throw "Assets root is missing: $AssetsRoot"
}
if (-not (Test-Path -LiteralPath $modelRoot -PathType Container)) {
    throw "Model asset root is missing: $modelRoot"
}
if (-not (Test-Path -LiteralPath $EditorExe -PathType Leaf)) {
    throw "Editor executable is missing: $EditorExe"
}
if ($TimeoutSec -le 0) {
    throw "TimeoutSec must be positive"
}

$supportedExtensions = @(".fbx", ".gltf", ".glb", ".obj")
$sources = @(Get-ChildItem -LiteralPath $AssetsRoot -Recurse -File |
    Where-Object { $supportedExtensions -contains $_.Extension.ToLowerInvariant() } |
    Sort-Object FullName)
if ($sources.Count -eq 0) {
    throw "No model sources were found under: $AssetsRoot"
}

# 캐시 이름은 현재 writer 규약상 stem.asset이다. stem 충돌이 있으면 두 원본이
# 같은 산출물을 덮어쓰므로 재생성에 들어가기 전에 중단한다.
$stemOwners = [Collections.Generic.Dictionary[string,string]]::new(
    [StringComparer]::OrdinalIgnoreCase)
foreach ($source in $sources) {
    if ($stemOwners.ContainsKey($source.BaseName)) {
        throw "Model source stem collision: $($stemOwners[$source.BaseName]) / $($source.FullName)"
    }
    $stemOwners.Add($source.BaseName, $source.FullName)
}

$stamp = Get-Date -Format "yyyyMMdd-HHmmss"
$runRoot = Join-Path $OutDir ("run-" + $stamp)
$backupRoot = Join-Path $runRoot "backup"
$commandFile = Join-Path $runRoot "commands.txt"
$stdoutFile = Join-Path $runRoot "editor.stdout.log"
$stderrFile = Join-Path $runRoot "editor.stderr.log"
$manifestFile = Join-Path $runRoot "manifest.csv"
New-Item -ItemType Directory -Path $backupRoot -Force | Out-Null

$expected = foreach ($source in $sources) {
    $cache = [IO.Path]::GetFullPath((Join-Path $modelRoot ($source.BaseName + ".asset")))
    if ([IO.Path]::GetFullPath((Split-Path $cache -Parent)) -ne $modelRoot) {
        throw "Refusing an unexpected model cache destination: $cache"
    }
    [pscustomobject]@{
        Source = $source.FullName
        Cache = $cache
        Backup = Join-Path $backupRoot ($source.BaseName + ".asset")
    }
}

$backedUp = [Collections.Generic.List[object]]::new()
$succeeded = $false
try {
    foreach ($item in $expected) {
        if (Test-Path -LiteralPath $item.Cache -PathType Leaf) {
            Move-Item -LiteralPath $item.Cache -Destination $item.Backup
            $backedUp.Add($item)
        }
    }

    # .asset.meta는 GUID 정본이므로 그대로 둔다. 캐시 payload만 새 포맷으로 교체한다.
    $commands = foreach ($item in $expected) {
        if ($item.Source.Contains('"')) {
            throw "A source path contains an unsupported quote: $($item.Source)"
        }
        'model.cache.build "' + $item.Source + '"'
    }
    $commands += "quit"
    [IO.File]::WriteAllLines(
        $commandFile, $commands, [Text.UTF8Encoding]::new($false))

    $editorRuntimeDirectory = Split-Path $EditorExe -Parent
    $process = Start-Process -FilePath $EditorExe `
        -ArgumentList @("--console", "--script", ('"{0}"' -f $commandFile)) `
        -WorkingDirectory $editorRuntimeDirectory `
        -WindowStyle Hidden `
        -RedirectStandardOutput $stdoutFile `
        -RedirectStandardError $stderrFile `
        -PassThru

    if (-not $process.WaitForExit($TimeoutSec * 1000)) {
        Stop-Process -Id $process.Id -Force
        throw "Editor timed out after $TimeoutSec seconds"
    }
    $process.WaitForExit()
    if ($process.ExitCode -ne 0) {
        throw "Editor exited with code $($process.ExitCode); see $stderrFile"
    }

    $stdout = Get-Content -LiteralPath $stdoutFile -Raw
    $failures = @([regex]::Matches(
        $stdout, '(?m)^\[model\.cache\.build\] FAIL'))
    $passes = @([regex]::Matches(
        $stdout, '(?m)^\[model\.cache\.build\] PASS'))
    if ($failures.Count -ne 0 -or $passes.Count -ne $expected.Count) {
        throw "Model rebuild command result mismatch: pass=$($passes.Count) fail=$($failures.Count) expected=$($expected.Count)"
    }

    $manifest = foreach ($item in $expected) {
        if (-not (Test-Path -LiteralPath $item.Cache -PathType Leaf)) {
            throw "Expected model cache was not generated: $($item.Cache)"
        }
        $bytes = [IO.File]::ReadAllBytes($item.Cache)
        if ($bytes.Length -lt 8 -or
            [Text.Encoding]::ASCII.GetString($bytes, 0, 4) -ne "CEMA" -or
            [BitConverter]::ToUInt32($bytes, 4) -ne 2) {
            throw "Generated model cache is not CEMA v2: $($item.Cache)"
        }

        [pscustomobject]@{
            Source = $item.Source
            Cache = $item.Cache
            Bytes = $bytes.Length
            SHA256 = (Get-FileHash -LiteralPath $item.Cache -Algorithm SHA256).Hash
            FormatVersion = 2
        }
    }
    $manifest | Export-Csv -LiteralPath $manifestFile -NoTypeInformation -Encoding utf8
    $succeeded = $true

    $totalBytes = ($manifest | Measure-Object -Property Bytes -Sum).Sum
    "model asset rebuild: PASS (sources=$($expected.Count), bytes=$totalBytes, format=CEMA-v2)"
    "manifest: $manifestFile"
    "backup: $backupRoot"
}
finally {
    if (-not $succeeded) {
        # 실패했을 때도 대상은 위에서 확정한 Models/<stem>.asset만 건드린다.
        foreach ($item in $expected) {
            if ([IO.Path]::GetFullPath((Split-Path $item.Cache -Parent)) -ne $modelRoot) {
                throw "Refusing rollback outside the model root: $($item.Cache)"
            }
            Remove-Item -LiteralPath $item.Cache -Force -ErrorAction SilentlyContinue
        }
        foreach ($item in $backedUp) {
            if (Test-Path -LiteralPath $item.Backup -PathType Leaf) {
                Move-Item -LiteralPath $item.Backup -Destination $item.Cache
            }
        }
    }
}
