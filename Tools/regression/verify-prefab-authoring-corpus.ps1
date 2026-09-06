param(
    [string]$Exe = (Join-Path $PSScriptRoot "..\..\Bin\x64-Debug\Editor\CreatorEditor.exe"),
    [string]$Work = $env:TEMP,
    [int]$TimeoutSeconds = 300
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

if (-not (Test-Path -LiteralPath $Exe -PathType Leaf)) {
    "실행 파일이 없다: $Exe"
    exit 1
}

$root = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
$prefabRoot = Join-Path $root 'Dynamic_CPP\Assets\Prefabs'
$prefabs = @(Get-ChildItem -LiteralPath $prefabRoot -File -Filter '*.prefab' |
    Sort-Object Name)
$expectedPrefabCount = 9
if ($prefabs.Count -ne $expectedPrefabCount) {
    "프리팹 코퍼스가 예상과 다르다: expected=$expectedPrefabCount actual=$($prefabs.Count)"
    $prefabs.Name
    exit 1
}

$sourceHashes = @{}
foreach ($prefab in $prefabs) {
    foreach ($path in @($prefab.FullName, ($prefab.FullName + '.meta'))) {
        if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
            "프리팹 target/meta pair가 없다: $path"
            exit 1
        }
        $sourceHashes[$path] = (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash
    }
}

$run = Join-Path $Work ("CE_D2PrefabCorpus_" + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $run -Force | Out-Null
$scenario = Join-Path $run 'commands.txt'
$stdout = Join-Path $run 'stdout.txt'
$stderr = Join-Path $run 'stderr.txt'
$roundTripScene = Join-Path $run 'D2PrefabCorpus.creator'
$names = @($prefabs | ForEach-Object { $_.BaseName })

$commands = [System.Collections.Generic.List[string]]::new()
$commands.Add('scene.new D2PrefabCorpus')
$commands.Add('wait 8')
foreach ($name in $names) {
    $commands.Add("prefab.instantiate $name D2Corpus_$name")
}
$commands.Add('wait 8')
$commands.Add("prefab.corpus.digest before $($names -join ' ')")
$commands.Add("scene.save $($roundTripScene.Replace('\', '/'))")
$commands.Add("scene.switch $($roundTripScene.Replace('\', '/'))")
$commands.Add('wait 12')
$commands.Add("prefab.corpus.digest after $($names -join ' ')")
$commands.Add('quit')
$commands | Set-Content -LiteralPath $scenario -Encoding UTF8

$process = Start-Process -FilePath $Exe -ArgumentList @('--commandlet-script', $scenario) `
    -WorkingDirectory $root -WindowStyle Hidden `
    -RedirectStandardOutput $stdout -RedirectStandardError $stderr -PassThru
$process.WaitForExit($TimeoutSeconds * 1000) | Out-Null
if (-not $process.HasExited) {
    $process.Kill()
    "TIMEOUT output=$run"
    exit 1
}

$text = if (Test-Path -LiteralPath $stdout) {
    Get-Content -LiteralPath $stdout -Raw
} else { '' }
$errorLines = if (Test-Path -LiteralPath $stderr) {
    @(Get-Content -LiteralPath $stderr)
} else { @() }
$knownLodWarning = 'Mesh::GenerateLODs: MeshOptimizer failed to generate LODs.'
$unexpectedErrors = @($errorLines | Where-Object {
    -not [string]::IsNullOrWhiteSpace($_) -and $_ -ne $knownLodWarning
})
$lodWarnings = @($errorLines | Where-Object { $_ -eq $knownLodWarning }).Count

$terminal = @($text -split "`n" | Where-Object { $_.TrimStart().StartsWith('{"schemaVersion"') } | ForEach-Object { $_ | ConvertFrom-Json })
$instantiates = @($terminal | Where-Object { $_.command -eq 'prefab.instantiate' -and $_.status -eq 'succeeded' }).Count
$snapshots = [regex]::Matches($text,
    '\[prefab\.corpus:(before|after)\] pass roots=(\d+)/(\d+) entities=(\d+) overrides=(\d+) registered=(\d+) digest=([0-9a-f]{16})')
$sourceMutations = [System.Collections.Generic.List[string]]::new()
foreach ($path in $sourceHashes.Keys) {
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
        $sourceMutations.Add("삭제됨: $path")
        continue
    }
    if ((Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash -ne $sourceHashes[$path]) {
        $sourceMutations.Add("변경됨: $path")
    }
}

$snapshotStable = $false
$before = $null
$after = $null
if ($snapshots.Count -eq 2) {
    $before = $snapshots[0]
    $after = $snapshots[1]
    $snapshotStable = $before.Groups[1].Value -eq 'before' -and
        $after.Groups[1].Value -eq 'after' -and
        $before.Groups[2].Value -eq $expectedPrefabCount -and
        $before.Groups[3].Value -eq $expectedPrefabCount -and
        $after.Groups[2].Value -eq $expectedPrefabCount -and
        $after.Groups[3].Value -eq $expectedPrefabCount -and
        $before.Groups[4].Value -eq $after.Groups[4].Value -and
        $before.Groups[5].Value -eq $after.Groups[5].Value -and
        $before.Groups[6].Value -eq $after.Groups[6].Value -and
        $before.Groups[7].Value -eq $after.Groups[7].Value
}

$entities = if ($before) { $before.Groups[4].Value } else { '?' }
$overrides = if ($before) { $before.Groups[5].Value } else { '?' }
$registered = if ($before) { $before.Groups[6].Value } else { '?' }
"prefab-authoring-corpus exit=$($process.ExitCode) output=$run"
"prefabs=$($prefabs.Count) instantiated=$instantiates/$expectedPrefabCount snapshots=$($snapshots.Count)/2 stable=$snapshotStable entities=$entities overrides=$overrides registered=$registered sourceMutations=$($sourceMutations.Count) lodWarnings=$lodWarnings unexpectedStderr=$($unexpectedErrors.Count)"

$passed = 0 -eq $process.ExitCode -and
    $instantiates -eq $expectedPrefabCount -and
    $snapshotStable -and
    (Test-Path -LiteralPath $roundTripScene -PathType Leaf) -and
    $sourceMutations.Count -eq 0 -and
    $unexpectedErrors.Count -eq 0
if (-not $passed) {
    if ($sourceMutations.Count -gt 0) { '원본 변경:'; $sourceMutations }
    if ($unexpectedErrors.Count -gt 0) { '예상하지 않은 stderr:'; $unexpectedErrors }
    exit 1
}

'전체 통과 — 프리팹 9개의 identity·override·등록 multiset이 씬 저장·재로드를 건넜다'
exit 0
