[CmdletBinding()]
param(
    [string]$Exe  = (Join-Path $PSScriptRoot "..\..\Bin\x64-Debug\Editor\CreatorEditor.exe"),
    [string]$Work = (Join-Path $env:TEMP "lc6op"),

    [ValidateRange(5, 900)]
    [int]$BootTimeoutSec = 180,

    # 지금 상태를 래칫에 다시 기록한다. **의도한 변경일 때만** 쓴다.
    [switch]$Update
)

# PHASE 14.5 LC6 (§9) — editor_operation 명령이 Undo 에 남기는 흔적을 못 박는다.
#
# ── 무엇을 지키려는가 ───────────────────────────────────────────────────
#
# §9 는 `editor_operation` 을 "GUI 와 Undo·selection·transaction 까지 같아야 하는
# 것"으로 정의한다. 그러면 그 14 개가 실제로 Undo 를 남기는지가 계약이다.
#
# ── 실측이 말한 것 ──────────────────────────────────────────────────────
#
# 서비스로 불러 `undo.state` 의 editUndo 를 앞뒤로 재 보면, **14 개 중 하나만**
# Undo 를 남긴다(`object.create.undoable`). `object.create` 도 `object.rename` 도
# `object.duplicate` 도 남기지 않는다. 사람이 GUI 로 한 것과 에이전트가 HTTP 로
# 한 것이 같은 조작인데 한쪽만 되돌릴 수 있다.
#
# ── 왜 고치지 않고 못 박는가 ────────────────────────────────────────────
#
# §9 의 완료 기준은 "GUI/서비스 의미 차이 **미분류** 0" 이다. 없애라가 아니라
# 분류하라다. 13 개에 Undo 를 붙이는 것은 각 명령의 저작 의미를 정하는 일이라
# 파일 분리와 같은 슬라이스에 들어갈 크기가 아니다.
#
# 그래서 지금 참인 것을 값으로 적어 둔다. 이 표가 바뀌면 — 누가 Undo 를 붙였든
# 있던 것을 떨어뜨렸든 — 붉어진다. 분류되지 않은 채 조용히 움직이는 것만 막는다.

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$ratchetPath = Join-Path $PSScriptRoot 'cli_editor_operation.ratchet.json'

if (-not (Test-Path -LiteralPath $Exe -PathType Leaf)) { "실행 파일이 없다: $Exe"; exit 1 }
$exeDir   = Split-Path -Parent $Exe
$repoRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
New-Item -ItemType Directory -Force -Path $Work | Out-Null

$endpointPath = Join-Path $repoRoot 'Dynamic_CPP\Library\CommandService\endpoint.json'

function Stop-AllEditors {
    Get-Process CreatorEditor -ErrorAction SilentlyContinue | ForEach-Object {
        try { $_.Kill(); $_.WaitForExit(20000) | Out-Null } catch { }
    }
    if (Test-Path -LiteralPath $endpointPath) { Remove-Item -LiteralPath $endpointPath -Force }
}

Stop-AllEditors
$proc = Start-Process -FilePath $Exe -ArgumentList '--command-service' -WorkingDirectory $exeDir `
    -RedirectStandardOutput (Join-Path $Work 'op.out') `
    -RedirectStandardError  (Join-Path $Work 'op.err') -PassThru

$deadline = (Get-Date).AddSeconds($BootTimeoutSec)
$info = $null
while ((Get-Date) -lt $deadline) {
    if (Test-Path -LiteralPath $endpointPath) {
        try {
            $parsed = Get-Content -LiteralPath $endpointPath -Raw | ConvertFrom-Json
            if ($parsed.pid -eq $proc.Id -and $parsed.port -gt 0) { $info = $parsed; break }
        } catch { }
    }
    Start-Sleep -Milliseconds 300
}
if ($null -eq $info) { Stop-AllEditors; "서비스가 뜨지 않았다"; exit 1 }

$base = "http://127.0.0.1:$($info.port)"
$auth = @{ Authorization = "Bearer $($info.token)" }

function Invoke-Cmd([string]$Body, [int]$TimeoutSec = 40) {
    $r = Invoke-WebRequest ($base + '/command') -Method POST -Headers $auth `
         -ContentType 'application/json' -Body $Body -UseBasicParsing `
         -SkipHttpErrorCheck -TimeoutSec ($TimeoutSec + 15)
    return ($r.Content | ConvertFrom-Json)
}

function Get-EditUndo {
    $j = Invoke-Cmd '{"command":"undo.state","mode":"sync"}'
    return [int]$j.data.editUndo
}

# 각 항목: 라벨 · 보낼 명령 · 이 명령을 부르기 전에 준비할 것
$cases = @(
    @{ Name = 'object.create';          Body = '{"command":"object.create","args":["OpProbeA"],"mode":"sync"}' }
    @{ Name = 'object.create.undoable'; Body = '{"command":"object.create.undoable","args":["OpProbeB"],"mode":"sync"}' }
    @{ Name = 'object.rename';          Body = '{"command":"object.rename","args":["OpProbeA","OpProbeA2"],"mode":"sync"}' }
    @{ Name = 'object.duplicate';       Body = '{"command":"object.duplicate","args":["OpProbeB"],"mode":"sync"}' }
    @{ Name = 'object.parent';          Body = '{"command":"object.parent","args":["OpProbeB","OpProbeA2"],"mode":"sync"}' }
    @{ Name = 'object.transform';       Body = '{"command":"object.transform","args":["OpProbeB","position","1","0","0"],"mode":"sync"}' }
    @{ Name = 'component.add';          Body = '{"command":"component.add","args":["OpProbeB","MeshRenderer"],"mode":"sync"}' }
    @{ Name = 'object.property';        Body = '{"command":"object.property","args":["OpProbeB","MeshRenderer","m_IsEnabled","false"],"mode":"sync"}' }
)

$observed = [ordered]@{}
try {
    foreach ($case in $cases) {
        $before = Get-EditUndo
        $null = Invoke-Cmd $case.Body
        $after = Get-EditUndo
        $records = ($after -gt $before)
        $observed[$case.Name] = $records
        "{0,-26} editUndo {1} -> {2}   undo={3}" -f `
            $case.Name, $before, $after, $(if ($records) { '기록함' } else { '기록 안 함' })
    }
}
finally { Stop-AllEditors }

if ($Update) {
    $payload = [ordered]@{
        schema   = 'lc6-editor-operation-undo v1'
        recorded = (Get-Date).ToString('yyyy-MM-dd')
        note     = 'editor_operation 명령이 편집 Undo 스택에 항목을 남기는가. 실측값이다.'
        undo     = $observed
    }
    $payload | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath $ratchetPath -Encoding UTF8
    "래칫을 지금 상태로 기록했다: $ratchetPath"
    exit 0
}

if (-not (Test-Path -LiteralPath $ratchetPath)) {
    "래칫이 없다: $ratchetPath (-Update 로 최초 기록)"
    exit 1
}
$ratchet = Get-Content -LiteralPath $ratchetPath -Raw | ConvertFrom-Json

$failures = New-Object System.Collections.Generic.List[string]
foreach ($name in $observed.Keys) {
    $expected = $ratchet.undo.$name
    if ($null -eq $expected) {
        $failures.Add("$name : 래칫에 없는 명령이다(새로 생겼으면 -Update)")
        continue
    }
    if ([bool]$expected -ne [bool]$observed[$name]) {
        $failures.Add(("{0} : Undo 기록이 {1} 에서 {2} 로 바뀌었다" -f `
            $name, $expected, $observed[$name]))
    }
}
foreach ($name in $ratchet.undo.PSObject.Properties.Name) {
    if (-not $observed.Contains($name)) {
        $failures.Add("$name : 래칫에 있는데 이번에 재지 못했다")
    }
}

""
if ($failures.Count -gt 0) {
    "실패:"
    $failures | ForEach-Object { "  - $_" }
    ""
    "Undo 기록을 **늘린** 것이라면 좋은 변경이다 — -Update 로 래칫을 옮기고 커밋에"
    "함께 담을 것. 줄었다면 §9 의 동등성이 그만큼 더 벌어진 것이다."
    exit 1
}
$recorded = @($observed.Values | Where-Object { $_ }).Count
"editor_operation Undo 특성화 통과 — {0}개 중 {1}개가 Undo 를 남긴다(래칫과 일치)" -f `
    $observed.Count, $recorded
exit 0
