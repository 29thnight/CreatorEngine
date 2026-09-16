# Scene 이름 등록부(m_entityNameSet)가 rename 과 그 Undo/Redo 를 따라가는가.
#
# ── 이 게이트가 메우는 구멍(2026-09-16) ──
#
# 새 엔티티 이름은 Scene::GenerateUniqueEntityName 이 등록부를 보고 " (N)" 을
# 붙여 정한다. 삭제는 RemoveEntityName 으로 이름을 돌려주지만 object.rename 의
# RenameCommand 는 `m_name.SetString` 만 불러 등록부를 건드리지 않았다. 그래서
#   ① 옛 이름이 영영 반환되지 않는다 — Prim_Cube 를 NameProbeAnchor 로 바꾼 뒤
#      다시 배치하면 "Prim_Cube" 가 비어 있는데도 "Prim_Cube (1)" 이 나온다.
#   ② 새 이름이 예약되지 않는다 — 같은 이름으로 만들면 중복이 그대로 나온다.
# Undo/Redo 도 같은 SetString 이라 같은 구멍을 가진다.
#
# ── 시나리오(한 프로세스) ──
#
#   place Prim_Cube → rename → NameProbeAnchor
#   place Prim_Cube            ① "Prim_Cube"            (옛 이름 반환)
#   create NameProbeAnchor     ② "NameProbeAnchor (1)"  (새 이름 예약)
#   undo·undo·undo(rename) → redo(rename)
#   place Prim_Cube            ③ "Prim_Cube"            (Redo 가 옛 이름 반환)
#   undo(place)·undo(rename)
#   create NameProbeAnchor     ④ "NameProbeAnchor"      (Undo 가 새 이름 반환)
#   create Prim_Cube           ⑤ "Prim_Cube (1)"        (Undo 가 옛 이름 재예약)
#
# ★ 이름은 명령 결과(data.name)로만 읽는다 — stdout 토큰은 판정에 쓰지 않는다.
param(
    [string]$Editor = (Join-Path $PSScriptRoot '..\..\Bin\x64-Debug\Editor\CreatorEditor.exe'),
    [string]$Work = $env:TEMP,
    [int]$TimeoutSeconds = 300
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'CommandResults.ps1')

$root = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
$run = Join-Path $Work ('creator-rename-registry-' + [guid]::NewGuid().ToString('N'))
$failures = [System.Collections.Generic.List[string]]::new()

function Invoke-Editor([string[]]$Commands) {
    $scenario = Join-Path $run 'rename.commands.txt'
    $resultPath = Join-Path $run 'rename.results.jsonl'
    [IO.File]::WriteAllText($scenario, ($Commands -join "`n") + "`n", [Text.UTF8Encoding]::new($false))
    $start = [Diagnostics.ProcessStartInfo]::new()
    $start.FileName = $Editor
    $start.Arguments = '--commandlet-script "' + $scenario + '" --result-file "' + $resultPath + '"'
    $start.WorkingDirectory = $root
    $start.UseShellExecute = $false
    $start.CreateNoWindow = $true
    $start.WindowStyle = [Diagnostics.ProcessWindowStyle]::Hidden
    $start.RedirectStandardOutput = $true
    $start.RedirectStandardError = $true
    $process = [Diagnostics.Process]::new()
    $process.StartInfo = $start
    if (-not $process.Start()) { throw "CreatorEditor 시작 실패: $Editor" }
    $stdoutTask = $process.StandardOutput.ReadToEndAsync()
    $stderrTask = $process.StandardError.ReadToEndAsync()
    if (-not $process.WaitForExit($TimeoutSeconds * 1000)) {
        # 강제 종료하지 않는다 — 떠 있는 프로세스와 작업 경로를 남기고 실패로 보고한다.
        throw "CreatorEditor timeout(pid $($process.Id)): $run"
    }
    $process.WaitForExit()
    $stdout = $stdoutTask.GetAwaiter().GetResult()
    $stderr = $stderrTask.GetAwaiter().GetResult()
    [IO.File]::WriteAllText((Join-Path $run 'rename.stdout.txt'), $stdout, [Text.UTF8Encoding]::new($false))
    [IO.File]::WriteAllText((Join-Path $run 'rename.stderr.txt'), $stderr, [Text.UTF8Encoding]::new($false))
    return [pscustomobject]@{ Results = @(Read-CommandResults $resultPath); Stderr = $stderr; ExitCode = $process.ExitCode }
}

try {
    if (-not (Test-Path -LiteralPath $Editor -PathType Leaf)) { throw "CreatorEditor 실행 파일이 없다: $Editor" }
    $baseScene = Join-Path $root 'Dynamic_CPP\Assets\Scenes\FT_Primitives.creator'
    $cube = Join-Path $root 'Dynamic_CPP\Assets\Models\Prim_Cube.glb'
    foreach ($path in @($baseScene, $cube)) {
        if (-not (Test-Path -LiteralPath $path -PathType Leaf)) { throw "fixture 가 없다: $path" }
    }
    New-Item -ItemType Directory -Path $run -Force | Out-Null
    if ($run -match '\s') { throw "console 인자로 넘길 작업 경로에 공백이 있다: $run" }

    $session = Invoke-Editor @(
        "scene.switch $($baseScene.Replace('\', '/'))",
        'wait 30',
        "model.loadcached $($cube.Replace('\', '/'))",
        'wait 60',
        'model.place Prim_Cube', 'wait 10',
        'object.rename Prim_Cube NameProbeAnchor', 'wait 10',
        'model.place Prim_Cube', 'wait 10',                 # ①
        'object.create NameProbeAnchor', 'wait 10',         # ②
        'undo', 'wait 10', 'undo', 'wait 10', 'undo', 'wait 10',
        'redo', 'wait 10',
        'model.place Prim_Cube', 'wait 10',                 # ③
        'undo', 'wait 10', 'undo', 'wait 10',
        'object.create NameProbeAnchor', 'wait 10',         # ④
        'object.create Prim_Cube', 'wait 10',               # ⑤
        'quit')

    if ($session.ExitCode -ne 0) { $failures.Add("종료 코드 $($session.ExitCode)") }
    if (-not [string]::IsNullOrWhiteSpace($session.Stderr)) { $failures.Add('stderr 가 비어 있지 않다.') }

    $judged = @($session.Results | Where-Object { $_.command -in @('model.place', 'object.create', 'object.rename', 'undo', 'redo') })
    foreach ($row in $judged) {
        if ($row.status -ne 'succeeded') { $failures.Add("$($row.command) status=$($row.status) code=$($row.code): $($row.message)") }
    }
    $named = @($judged | Where-Object { $_.command -in @('model.place', 'object.create', 'object.rename') })
    $actual = @($named | ForEach-Object { if ($_.status -eq 'succeeded') { "$($_.command)=$($_.data.name)" } else { "$($_.command)=<$($_.status)>" } })
    $expected = @(
        'model.place=Prim_Cube',
        'object.rename=NameProbeAnchor',
        'model.place=Prim_Cube',                # ① 옛 이름 반환
        'object.create=NameProbeAnchor (1)',    # ② 새 이름 예약
        'model.place=Prim_Cube',                # ③ Redo 가 옛 이름 반환
        'object.create=NameProbeAnchor',        # ④ Undo 가 새 이름 반환
        'object.create=Prim_Cube (1)')          # ⑤ Undo 가 옛 이름 재예약
    if ($actual.Count -ne $expected.Count) {
        $failures.Add("이름 결과 수 $($actual.Count) != $($expected.Count): $($actual -join ' | ')")
    } else {
        for ($i = 0; $i -lt $expected.Count; ++$i) {
            if ($actual[$i] -cne $expected[$i]) { $failures.Add("step $($i + 1): expected '$($expected[$i])' got '$($actual[$i])'") }
        }
    }
    Write-Host "rename registry names: $($actual -join ' | ')"
} catch {
    $failures.Add($_.Exception.Message)
}

if ($failures.Count -gt 0) {
    Write-Host "FAIL entity rename name registry ($run)"
    $failures | ForEach-Object { Write-Host "  - $_" }
    exit 1
}
Remove-Item -LiteralPath $run -Recurse -Force -ErrorAction SilentlyContinue
Write-Host 'PASS entity rename name registry'
exit 0
