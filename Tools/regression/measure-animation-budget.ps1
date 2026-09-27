[CmdletBinding()]
param(
    [ValidateSet('Debug','Release')][string]$Configuration = 'Release',
    [ValidateSet(10,50,100)][int]$Actors = 100,
    [ValidateRange(0.01,100)][double]$LowBudgetMs = 1.0,
    [ValidateRange(0.01,100)][double]$HighBudgetMs = 2.0,
    [ValidateRange(0.01,100)][double]$ConfiguredBudgetMs = 4.0,
    [string]$Work = ''
)
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
if ($LowBudgetMs -ge $HighBudgetMs) { throw 'HighBudgetMs must exceed LowBudgetMs.' }
if ($HighBudgetMs -ge $ConfiguredBudgetMs) { throw 'ConfiguredBudgetMs must exceed HighBudgetMs.' }
$repo = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
if (-not $Work) { $Work = Join-Path $repo ('Build/Obj/Phase13S5/Budget-' + [guid]::NewGuid().ToString('N')) }
$Work = [IO.Path]::GetFullPath($Work)
if (Test-Path -LiteralPath $Work) { throw 'Use a new budget measurement directory.' }
if (Get-Process CreatorEditor -ErrorAction SilentlyContinue) { throw 'Close the existing Editor before measurement.' }
$exe = Join-Path $repo "Bin/x64-$Configuration/Editor/CreatorEditor.exe"
$model = Join-Path $repo 'Dynamic_CPP/Assets/Models/CreatorRobot.glb'
if (-not (Test-Path -LiteralPath $exe) -or -not (Test-Path -LiteralPath $model)) {
    throw 'Build the Editor and keep the CreatorRobot fixture available.'
}
New-Item -ItemType Directory -Path $Work | Out-Null
$scenario = Join-Path $Work 'scenario.txt'
$resultsPath = Join-Path $Work 'results.jsonl'
$commands = @('scene.new AnimationBudgetProbe', 'play', 'wait 2', 'play.pause')
foreach ($budget in @($LowBudgetMs, $HighBudgetMs, $ConfiguredBudgetMs)) {
    $commands += 'animation.baseline.probe "' + $model + '" ' + $Actors + ' budget ' +
        $budget.ToString('0.###', [Globalization.CultureInfo]::InvariantCulture)
}
$commands += @('stop', 'wait 60')
$commands | Set-Content -LiteralPath $scenario -Encoding utf8
$proc = Start-Process -FilePath $exe -WorkingDirectory (Split-Path $exe) -WindowStyle Hidden `
    -ArgumentList @('--commandlet-script', ('"' + $scenario + '"'), '--result-file', ('"' + $resultsPath + '"')) `
    -RedirectStandardOutput (Join-Path $Work 'editor.out') -RedirectStandardError (Join-Path $Work 'editor.err') -PassThru
try {
    if (-not $proc.WaitForExit(600000)) { throw 'Animation budget probe timed out.' }
    $proc.Refresh()
    if (-not (Test-Path -LiteralPath $resultsPath)) { throw 'Animation budget results are missing.' }
    $results = @(Get-Content -LiteralPath $resultsPath | ConvertFrom-Json)
    if ($proc.ExitCode -ne 0 -or $results.Count -ne $commands.Count -or
        @($results | Where-Object status -ne succeeded).Count) {
        $results | Select-Object command,status,message | Format-Table -AutoSize |
            Out-String | Write-Output
        throw "Animation budget probe failed: $Work"
    }
    $probes = @($results | Where-Object command -eq 'animation.baseline.probe')
    if ($probes.Count -ne 3) { throw 'Expected stress, doubled, and configured budget probes.' }
    $rows = @()
    foreach ($probe in $probes) {
        $data = $probe.data
        if (-not $data.passed -or $data.frames.Count -ne 120) { throw 'Incomplete budget probe.' }
        $frames = @($data.frames)
        if (@($frames | Where-Object { $_.budgetEligible -ne $Actors -or
            (@($_.qualityStageCounts | Measure-Object -Sum)[0].Sum) -ne $Actors }).Count) {
            throw 'Budget camera or stage accounting did not cover all actors.'
        }
        $errors = @($frames | ForEach-Object {
            if ($_.measuredPoseUs -gt 0) {
                [math]::Abs($_.predictedPoseUs - $_.measuredPoseUs) / $_.measuredPoseUs
            }
        } | Sort-Object)
        $overruns = @($frames | Where-Object budgetOverrun -eq 1).Count
        $gradeSum = 0.0
        foreach ($frame in $frames) {
            for ($stage = 0; $stage -le 7; ++$stage) {
                $gradeSum += $stage * [double]$frame.qualityStageCounts[$stage]
            }
        }
        $rows += [pscustomobject]@{
            budgetMs = [double]$data.budgetMs
            actors = $Actors
            frames = $frames.Count
            overrunFrames = $overruns
            predictionErrorP50 = $errors[[int][math]::Floor($errors.Count * .5)]
            predictionErrorP95 = $errors[[int][math]::Floor($errors.Count * .95)]
            averageGrade = $gradeSum / ($frames.Count * $Actors)
        }
    }
    $summary = [ordered]@{
        configuration = $Configuration
        head = (git -C $repo rev-parse HEAD)
        editorSha256 = (Get-FileHash -LiteralPath $exe).Hash
        modelSha256 = (Get-FileHash -LiteralPath $model).Hash
        rows = $rows
    }
    $summary | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath (Join-Path $Work 'budget-summary.json') -Encoding utf8
    $rows | Format-Table -AutoSize | Out-String | Write-Output
    Write-Output "ANIMATION_BUDGET_ARTIFACT $Work"
    if ($rows[1].averageGrade -gt $rows[0].averageGrade + .001) {
        throw 'Doubling the budget reduced average animation quality.'
    }
    if ($rows[2].overrunFrames -ge 2) {
        throw "Configured 100-actor budget was exceeded in at least 1% of frames: $Work"
    }
    foreach ($row in $rows) {
        if ($row.predictionErrorP50 -gt .15) {
            throw "Median task-cost prediction error exceeded 15% at $($row.budgetMs) ms: $Work"
        }
    }
}
finally {
    if (-not $proc.HasExited) { $proc.Kill(); $proc.WaitForExit() }
}
