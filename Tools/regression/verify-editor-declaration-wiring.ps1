[CmdletBinding()]
param(
    [string]$Exe = (Join-Path $PSScriptRoot '../../Bin/x64-Debug/Editor/CreatorEditor.exe'),
    [string]$Work = (Join-Path $env:TEMP 'creator-editor-declaration')
)
# PHASE 21 M4 4단계 — `editor::` 선언 배선 게이트.
#
# 두 가지를 한 번에 태운다.
#
# ① `editor.selftest` — 창 표와 메뉴 표의 선언 자가 검사. 둘 다 자기 표를 옆으로
#    치우고 합성 선언 위에서 돌므로 살아 있는 에디터에서 부를 수 있다. 처음에는
#    "표가 비어 있을 때만"이라 부팅 전 한순간에만 돌 수 있었고, 그 말은 도는
#    세트에 넣을 수 없다는 뜻이었다. 이 저장소는 "게이트가 도는 세트에 없으면
#    없는 것"으로 두 번 데었다.
#
# ② `editor.windows` — 선언 표와 본문 보관소를 맞대 본다. 어느 한쪽에만 있는
#    이름이 곧 조용히 죽은 창이고, 그것을 양방향으로 본다. 오타 하나가 영영 비어
#    있는 창을 만들던 옛 `GetContext` 결함의 반대편이 새로 생겼기 때문이다 —
#    새 창구는 없는 이름에 아무 일도 하지 않는데, 그러면 이번에는 아무 일도
#    일어나지 않아서 오타가 안 보인다.
#
# ★ 숫자를 못 박지 않는다. 창 개수를 여기 적으면 창을 하나 더할 때마다 이
#   게이트가 이유 없이 붉어지고, 그러면 사람이 숫자만 고치고 지나간다. 이
#   게이트가 지키는 것은 개수가 아니라 **배선이 성립하는가**다.
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$Exe = [IO.Path]::GetFullPath($Exe)
$Work = [IO.Path]::GetFullPath($Work)
New-Item -ItemType Directory -Force -Path $Work | Out-Null

if (-not (Test-Path -LiteralPath $Exe)) { throw "Editor executable not found: $Exe" }
# 프로젝트에 endpoint 파일은 하나다. 개발자가 띄워 둔 에디터를 죽이지 않는다.
if (Get-Process CreatorEditor -ErrorAction SilentlyContinue) { throw 'Close the existing editor before running this isolated gate.' }

$script:checks = 0
function Assert([bool]$Condition, [string]$Message) {
    if (-not $Condition) { throw $Message }
    $script:checks++
}

$scriptPath = Join-Path $Work 'declaration.txt'
$resultPath = Join-Path $Work 'declaration.jsonl'
$stdoutPath = Join-Path $Work 'declaration.out'
$stderrPath = Join-Path $Work 'declaration.err'
foreach ($file in @($resultPath, $stdoutPath, $stderrPath)) {
    if (Test-Path -LiteralPath $file) { Remove-Item -LiteralPath $file }
}
Set-Content -LiteralPath $scriptPath -Encoding UTF8 -Value @('editor.selftest', 'editor.windows', 'quit')

$proc = Start-Process -FilePath $Exe -WorkingDirectory (Split-Path $Exe) -WindowStyle Hidden -PassThru `
    -ArgumentList @('--script', ('"' + $scriptPath + '"'), '--result-format', 'jsonl',
                    '--result-file', ('"' + $resultPath + '"')) `
    -RedirectStandardOutput $stdoutPath -RedirectStandardError $stderrPath
if (-not $proc.WaitForExit(300000)) {
    $proc.Kill()
    throw 'Editor did not exit within 300s while running the declaration gate.'
}
Assert ($proc.ExitCode -eq 0) "Declaration gate batch exited $($proc.ExitCode); see $stdoutPath"

Assert (Test-Path -LiteralPath $resultPath) "No result file produced at $resultPath"
$lines = @(Get-Content -LiteralPath $resultPath | Where-Object { $_.Trim().Length -gt 0 })
$results = @{}
foreach ($line in $lines) {
    $parsed = $line | ConvertFrom-Json
    $results[$parsed.command] = $parsed
}

# ── ① 선언 자가 검사 ────────────────────────────────────────────────────
Assert ($results.ContainsKey('editor.selftest')) 'editor.selftest produced no result line'
$selftest = $results['editor.selftest']
Assert ($selftest.status -eq 'succeeded') "editor.selftest failed: $($selftest.message)"
Assert ($selftest.data.windows -eq $true) "Window declaration selftest failed: $($selftest.data.windowReport)"
Assert ($selftest.data.menus -eq $true) "Menu declaration selftest failed: $($selftest.data.menuReport)"

# ── ② 창 배선 감사 ──────────────────────────────────────────────────────
Assert ($results.ContainsKey('editor.windows')) 'editor.windows produced no result line'
$windows = $results['editor.windows']
Assert ($windows.status -eq 'succeeded') "editor.windows failed: $($windows.message)"
Assert ($windows.data.clean -eq $true) "Window wiring audit is dirty: $($windows.message)"
Assert ($windows.data.orphanBodies -eq 0) "Bodies bound to undeclared window ids: $($windows.data.orphanBodies)"
Assert ($windows.data.bodylessWindows -eq 0) "Declared windows with no body bound: $($windows.data.bodylessWindows)"
Assert ($windows.data.duplicateIds -eq 0) "Duplicate stable ids: $($windows.data.duplicateIds)"
Assert ($windows.data.emptyDockSlots -eq 0) "Dock slots no window targets: $($windows.data.emptyDockSlots)"

# 표가 비어 있는데 초록으로 지나가는 것을 막는다. 빈 집합을 성공으로 읽는 것이
# 이 저장소에서 두 번 나온 실패 양식이다.
Assert ($windows.data.declared -gt 0) 'Window declaration table is empty; the gate would be vacuous'
Assert ($windows.data.boundBodies -gt 0) 'No window bodies are bound; the gate would be vacuous'

# 표에 도크 자리를 가진 창이 실제로 있어야 한다 — 전부 floating 이면 위의 빈 자리
# 단정이 자동으로 통과한다.
$table = Get-Content -LiteralPath $stdoutPath
Assert (($table | Where-Object { $_ -match "`tcenter`t" }).Count -gt 0) 'No window declares the center dock slot'

"editor:: declaration wiring OK — declared=$($windows.data.declared) bound=$($windows.data.boundBodies) checks=$($script:checks)"
exit 0
