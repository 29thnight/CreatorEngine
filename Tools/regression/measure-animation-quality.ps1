[CmdletBinding()]
param(
    [ValidateSet('Release','Debug')][string]$Configuration = 'Release',
    [ValidateRange(1,5)][int]$Repeats = 3,
    [ValidateSet(10,50,100)][int]$Actors = 100,
    [string]$Work = ''
)
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$repo = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
if (-not $Work) { $Work = Join-Path $repo ('Build/Obj/Phase13S4/Quality-' + [guid]::NewGuid().ToString('N')) }
$Work = [IO.Path]::GetFullPath($Work)
if (Test-Path -LiteralPath $Work) { throw 'Use a new quality measurement directory.' }
if (Get-Process CreatorEditor -ErrorAction SilentlyContinue) { throw 'Close the existing Editor before measurement.' }
$exe = Join-Path $repo "Bin/x64-$Configuration/Editor/CreatorEditor.exe"
$model = Join-Path $repo 'Dynamic_CPP/Assets/Models/CreatorRobot.glb'
if (-not (Test-Path -LiteralPath $exe) -or -not (Test-Path -LiteralPath $model)) {
    throw 'Build the Editor and keep the CreatorRobot fixture available.'
}
New-Item -ItemType Directory -Path $Work | Out-Null
$rows = @()
for ($repeat = 1; $repeat -le $Repeats; ++$repeat) {
    $run = Join-Path $Work "run-$repeat"
    New-Item -ItemType Directory -Path $run | Out-Null
    $scenario = Join-Path $run 'scenario.txt'
    $result = Join-Path $run 'results.jsonl'
    $commands = @('scene.new AnimationQualityCost', 'play', 'wait 2', 'play.pause')
    foreach ($stage in 0..7) {
        $commands += 'animation.baseline.probe "' + $model + '" ' + $Actors + ' ' + $stage
    }
    $commands += 'play'
    $commands | Set-Content -LiteralPath $scenario -Encoding utf8
    $proc = Start-Process -FilePath $exe -WorkingDirectory (Split-Path $exe) -WindowStyle Hidden `
        -ArgumentList @('--commandlet-script', ('"' + $scenario + '"'), '--result-file', ('"' + $result + '"')) `
        -RedirectStandardOutput (Join-Path $run 'editor.out') -RedirectStandardError (Join-Path $run 'editor.err') -PassThru
    try {
        if (-not $proc.WaitForExit(600000)) { throw "Quality run timed out: $run" }
        $proc.Refresh()
        if (-not (Test-Path -LiteralPath $result)) { throw "Missing quality results: $run" }
        $results = @(Get-Content -LiteralPath $result | ConvertFrom-Json)
        $failed = @($results | Where-Object status -ne succeeded)
        if ($proc.ExitCode -ne 0 -or $results.Count -ne $commands.Count -or $failed.Count) {
            $failed | ConvertTo-Json -Depth 5 | Write-Output
            throw "Quality run failed: $run"
        }
        $probes = @($results | Where-Object command -eq 'animation.baseline.probe')
        if ($probes.Count -ne 8) { throw "Expected eight quality probes: $run" }
        foreach ($probe in $probes) {
            $data = $probe.data
            if (-not $data.passed -or $data.actors -ne $Actors -or $data.frames.Count -ne 120) {
                throw "Incomplete quality probe: $run"
            }
            $row = [ordered]@{ repeat=$repeat; stage=[int]$data.qualityStage; actors=$Actors;
                bones=$data.bones; lowDetailBoneCount=$data.lowDetailBoneCount; frames=120 }
            foreach ($metric in @('executePassWorkerSumUs','workerSumUs','updateUs','syncUs','renderCommitUs')) {
                $values = @($data.frames | ForEach-Object { [double]$_.$metric } | Sort-Object)
                if (@($values | Where-Object { -not [double]::IsFinite($_) -or $_ -lt 0 }).Count) {
                    throw "Invalid $metric at stage $($data.qualityStage)"
                }
                $row["${metric}P50"] = $values[59]
                $row["${metric}P95"] = $values[113]
            }
            $rows += [pscustomobject]$row
            Write-Output ('repeat={0} L{1} actors={2} execute-p50={3:N3}ms update-p50={4:N3}ms' -f `
                $repeat,$data.qualityStage,$Actors,($row.executePassWorkerSumUsP50/1000),($row.updateUsP50/1000))
        }
    }
    finally {
        if (-not $proc.HasExited) { $proc.Kill(); $proc.WaitForExit() }
    }
}
$stageMedians = @()
foreach ($stage in 0..7) {
    $samples = @($rows | Where-Object stage -eq $stage | ForEach-Object { $_.executePassWorkerSumUsP50 } | Sort-Object)
    $stageMedians += $samples[[int][math]::Floor($samples.Count / 2)]
}
$regressions = @()
for ($stage = 1; $stage -le 7; ++$stage) {
    if ($stageMedians[$stage] -gt $stageMedians[$stage-1] * 1.10) {
        $regressions += "L$($stage-1)->L$stage"
    }
}
$artifact = [ordered]@{ configuration=$Configuration; actors=$Actors; repeats=$Repeats;
    model=$model; modelSha256=(Get-FileHash -LiteralPath $model).Hash;
    editorSha256=(Get-FileHash -LiteralPath $exe).Hash;
    head=(git -C $repo rev-parse HEAD);
    clock='MSVC steady_clock / QPC'; unit='microseconds';
    measured='Product AnimationScheduler execute-worker sum, update, sync and render commit; excludes GPU';
    stageExecuteP50Medians=$stageMedians; overTenPercentRegressions=$regressions; rows=$rows }
$artifact | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath (Join-Path $Work 'quality-cost.json') -Encoding utf8
Write-Output "ANIMATION_QUALITY_COST_ARTIFACT $Work"
if ($regressions.Count) { throw "Measured execute cost rose over 10%: $($regressions -join ', ')" }
