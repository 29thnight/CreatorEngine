# TransformUpdatePlan X3 canonical hierarchy mutation gate.
param(
    [string]$Exe = (Join-Path $PSScriptRoot "..\..\Bin\x64-Release\Editor\CreatorEditor.exe"),
    [string]$Work = $env:TEMP,
    [int]$TimeoutSeconds = 120
)

$repo = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot "..\.."))
$failed = $false
$markers = @(
    [pscustomobject]@{ Path = "Engine\SceneRuntime\Scene.h"; Pattern = "ReparentResult Reparent(EntityHandle child, EntityHandle newParent);"; Label = "handle reparent API" },
    [pscustomobject]@{ Path = "Engine\SceneRuntime\Scene.h"; Pattern = "std::atomic<uint64_t> m_topologyVersion"; Label = "topology version" },
    [pscustomobject]@{ Path = "Engine\SceneRuntime\Scene.h"; Pattern = "HierarchyBulkBuildScope BeginHierarchyBulkBuild();"; Label = "bulk-build transaction" },
    [pscustomobject]@{ Path = "Engine\SceneRuntime\Entity.h"; Pattern = "X3: 런타임 계층 변경은 Scene::Reparent만 사용한다."; Label = "private low-level mutation boundary" },
    [pscustomobject]@{ Path = "Engine\SceneRuntime\SceneManager.cpp"; Pattern = "BeginHierarchyBulkBuild();"; Label = "loader bulk-build wiring" }
)

foreach ($marker in $markers) {
    $path = Join-Path $repo $marker.Path
    if (-not (Test-Path $path) -or
        -not [System.IO.File]::ReadAllText($path).Contains($marker.Pattern)) {
        "FAIL: missing $($marker.Label) marker in $($marker.Path)"
        $failed = $true
    }
}

# Runtime callers may not assemble parent/children relationships piecemeal.
# Definitions plus Scene/SceneManager internals are the only allowed raw surface.
$sourceFiles = Get-ChildItem -Path (Join-Path $repo "Engine"),(Join-Path $repo "Editor") `
    -Recurse -File -Include *.h,*.cpp
$rawMutations = @($sourceFiles | Select-String -Pattern `
    '\b(SetParentIndex|AttachChildIndex|DetachChildIndex|ClearChildren|SetChildrenIndices)\s*\(' |
    Where-Object {
        $_.Path -notlike '*\Engine\SceneRuntime\Entity.h' -and
        $_.Path -notlike '*\Engine\SceneRuntime\Entity.cpp' -and
        $_.Path -notlike '*\Engine\SceneRuntime\HierarchyStore.h' -and
        $_.Path -notlike '*\Engine\SceneRuntime\Scene.cpp' -and
        $_.Path -notlike '*\Engine\SceneRuntime\SceneManager.cpp'
    })
if ($rawMutations.Count -ne 0) {
    "FAIL: hierarchy mutation bypasses Scene::Reparent: $($rawMutations.Count)"
    $rawMutations | ForEach-Object { "  $($_.Path):$($_.LineNumber):$($_.Line.Trim())" }
    $failed = $true
}

if ($failed) { exit 1 }
if (-not (Test-Path $Exe)) { "FAIL: executable missing: $Exe"; exit 1 }

$runId = [guid]::NewGuid().ToString('N')
$scenario = Join-Path $Work "hierarchy_mutation_$runId.txt"
$outFile = Join-Path $Work "hierarchy_mutation_$runId.out"
$errFile = Join-Path $Work "hierarchy_mutation_$runId.err"
[System.IO.File]::WriteAllLines($scenario, @(
    "scene.hierarchymutation probe",
    "scene.hierarchycheck",
    "quit"
))

$exeDir = [System.IO.Path]::GetDirectoryName($Exe)
$proc = Start-Process -FilePath $Exe -ArgumentList "--commandlet-script", $scenario `
    -WorkingDirectory $exeDir -WindowStyle Hidden `
    -RedirectStandardOutput $outFile -RedirectStandardError $errFile -PassThru
$proc.WaitForExit($TimeoutSeconds * 1000) | Out-Null
if (-not $proc.HasExited) {
    $proc.Kill()
    "FAIL: runtime probe timeout ($TimeoutSeconds seconds)"
    exit 1
}

$output = [System.IO.File]::ReadAllText($outFile)
$resultLine = 'nochange=no-change self=self-rejected ancestor=cycle-rejected stale=stale-handle cross=cross-scene rejected-delta=0 success=success success-delta=1'
$metricLine = 'symmetry=0 mismatch=0 orphan=0 duplicate=0 invalid=0 bulk-delta=1 clean-delta=0'
$hierarchyLine = '쌍불일치 0.*고아 0.*순회미도달 0.*Store불일치 0'
if ($proc.ExitCode -ne 0 -or
    $output -notmatch [regex]::Escape($resultLine) -or
    $output -notmatch [regex]::Escape($metricLine) -or
    $output -notmatch '\[scene\.hierarchymutation\] probe=PASS' -or
    $output -notmatch $hierarchyLine) {
    "FAIL: X3 runtime hierarchy mutation probe"
    $output -split "`r?`n" | Where-Object { $_ -match 'scene\.(hierarchymutation|hierarchycheck)' }
    if (Test-Path $errFile) { Get-Content $errFile }
    exit 1
}

"PASS: X3 canonical reparent rejects self/ancestor/stale/cross-scene, symmetry=0, bulk delta=1, clean delta=0"
exit 0
