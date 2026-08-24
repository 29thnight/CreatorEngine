# Scene Entity API/직렬화 키 전환 게이트.
#
# 1) 새 저장은 m_Entities를 쓴다.
# 2) 같은 파일의 키를 구형 m_SceneObjects로 바꿔도 읽힌다.
# 3) 로드 결과의 계층과 H1 shadow HierarchyStore가 일치한다.
param(
    [string]$Exe = (Join-Path $PSScriptRoot "..\..\Bin\x64-Debug\Editor\CreatorEditor.exe"),
    [string]$Work = $env:TEMP,
    [int]$TimeoutSeconds = 300
)

$exeDir = [System.IO.Path]::GetDirectoryName($Exe)
if (-not (Test-Path $Exe)) { "실행 파일이 없다: $Exe"; exit 1 }

$currentScene = Join-Path $Work "SceneEntityApiCurrent.creator"
$legacyScene  = Join-Path $Work "SceneEntityApiLegacy.creator"
$authorScript = Join-Path $Work "scene_entity_api_author.txt"
$loadScript   = Join-Path $Work "scene_entity_api_load.txt"
$authorOut    = Join-Path $Work "scene_entity_api_author.out"
$authorErr    = Join-Path $Work "scene_entity_api_author.err"
$loadOut      = Join-Path $Work "scene_entity_api_load.out"
$loadErr      = Join-Path $Work "scene_entity_api_load.err"

foreach ($path in @($currentScene, $legacyScene)) {
    if (Test-Path $path) { Remove-Item -LiteralPath $path -Force }
}

$currentForCli = $currentScene -replace '\\', '/'
$legacyForCli = $legacyScene -replace '\\', '/'

@"
wait 60
scene.new SceneEntityApiProbe
wait 20
object.create EntityApiParent Empty
object.create EntityApiChild Empty
object.parent EntityApiChild EntityApiParent
wait 20
scene.save $currentForCli
wait 30
log.flush
quit
"@ | Set-Content -LiteralPath $authorScript -Encoding UTF8

function Invoke-Probe([string]$script, [string]$outPath, [string]$errPath) {
    $proc = Start-Process -FilePath $Exe -ArgumentList "--script", $script `
        -WorkingDirectory $exeDir -RedirectStandardOutput $outPath `
        -RedirectStandardError $errPath -PassThru
    $proc.WaitForExit($TimeoutSeconds * 1000) | Out-Null
    if (-not $proc.HasExited) {
        $proc.Kill()
        throw "타임아웃 ($TimeoutSeconds 초): $script"
    }
    if ($proc.ExitCode -ne 0) {
        throw ("종료 코드 비정상 0x{0:X8}: {1}" -f $proc.ExitCode, $script)
    }
}

try {
    Invoke-Probe $authorScript $authorOut $authorErr
}
catch {
    "새 Scene 저장 실행 실패: $_"
    exit 1
}

if (-not (Test-Path $currentScene)) {
    "새 Scene 파일이 저장되지 않았다: $currentScene"
    exit 1
}

$currentText = Get-Content -LiteralPath $currentScene -Raw
if ($currentText -notmatch '(?m)^m_Entities:\s*$') {
    "새 저장 결과에 m_Entities 키가 없다"
    exit 1
}
if ($currentText -match '(?m)^m_SceneObjects:\s*$') {
    "새 저장 결과에 구 키 m_SceneObjects가 남아 있다"
    exit 1
}

$legacyText = $currentText -replace '(?m)^m_Entities:\s*$', 'm_SceneObjects:'
Set-Content -LiteralPath $legacyScene -Value $legacyText -Encoding UTF8

@"
wait 60
scene.switch $legacyForCli
wait 120
scene.hierarchycheck
log.flush
quit
"@ | Set-Content -LiteralPath $loadScript -Encoding UTF8

try {
    Invoke-Probe $loadScript $loadOut $loadErr
}
catch {
    "구 Scene 키 로드 실행 실패: $_"
    exit 1
}

$loadText = Get-Content -LiteralPath $loadOut -Raw
$pattern = '\[scene\.hierarchycheck\] 오브젝트 (\d+).*쌍불일치 (\d+) · 고아 (\d+) · 순회미도달 (\d+) · Store불일치 (\d+)'
$match = [regex]::Match($loadText, $pattern)
if (-not $match.Success) {
    "구 키 파일 로드 뒤 계층 측정값이 없다: $loadOut"
    exit 1
}

$total = [int]$match.Groups[1].Value
$pairMismatch = [int]$match.Groups[2].Value
$orphan = [int]$match.Groups[3].Value
$unreachable = [int]$match.Groups[4].Value
$storeMismatch = [int]$match.Groups[5].Value

"새 저장 키 m_Entities 확인"
"구 읽기 키 m_SceneObjects 확인 — 오브젝트 $total · 쌍불일치 $pairMismatch · 고아 $orphan · 순회미도달 $unreachable · Store불일치 $storeMismatch"

if ($total -lt 3 -or $pairMismatch -ne 0 -or $orphan -ne 0 -or
    $unreachable -ne 0 -or $storeMismatch -ne 0) {
    "실패 — 구 키 로드 결과가 저작한 Entity 계층을 복원하지 못했다"
    exit 1
}

"전체 통과 — Scene은 Entity 키로 저장하고 구 GameObject 키를 읽기 호환한다"
exit 0
