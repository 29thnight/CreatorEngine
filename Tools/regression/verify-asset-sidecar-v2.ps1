# model sidecar schema v2 · stable key · identity epoch header (MBC2)
#
# `assets.sidecar <assetRoot>`를 실자산 corpus 위에서 돌린다. selftest가 재는 것:
#   [1] epoch header CSPRNG·YAML 왕복·변조 거부(프로필/seed 길이/대문자/0 seed/schema)
#   [2] stable key 문법 — ordinal(`gltf/material/0`)·빈 값·비NFC·대문자 접두 거부
#   [3] 규칙 엔진 — semantic/authoring 배정, 지문 재결합(순서 무관), 삭제=경고,
#       내용 변경+무명=오류, exporter id 중복=오류
#   [4] v2 코덱 — 왕복·다른 최상위 키 보존·legacy guid 제거·v1/ordinal/변조 거부·폐포 재유도
#   [5] corpus — 모델마다 임포트→배정→v2 생성→폐포, 전 모델을 한 registry에 넣어 충돌 0,
#       같은 입력 재배정이 같은 신원
#
# 이 게이트가 더하는 실자산 단정(MBC0 기준선 §2.1의 실측을 고정):
#   · Gunner_F_Mythic: 재질 2·임베디드 6이 **전부 semantic**(이름 유일)
#   · scene.glb: 재질 25가 **전부 authoring**(전부 무명) — authoring 경로가 실자산에서 실제로 돈다
#   · 모델 수 ≥ tracked 11
# 디스크에는 아무것도 쓰지 않는다 — sidecar 원본 해시가 전후 동일해야 한다(쓰기는 MBC3).
param(
    [string]$Exe = (Join-Path $PSScriptRoot "..\..\Bin\x64-Debug\Editor\CreatorEditor.exe"),
    [string]$Work = $env:TEMP,
    [int]$TimeoutSeconds = 300
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

if (-not (Test-Path -LiteralPath $Exe -PathType Leaf)) { "실행 파일이 없다: $Exe"; exit 1 }
$Exe = (Resolve-Path -LiteralPath $Exe).Path
$root = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
$assets = Join-Path $root 'Dynamic_CPP\Assets'
if (-not (Test-Path -LiteralPath $assets -PathType Container)) { "asset root가 없다: $assets"; exit 1 }

# 원본 불변 — sidecar/모델 파일 해시 전후 대조
$watched = @(Get-ChildItem -LiteralPath $assets -Recurse -File | Where-Object {
    $_.Extension.ToLowerInvariant() -in @('.glb', '.gltf', '.fbx') -or $_.Name -match '\.(glb|gltf|fbx)\.meta$'
})
$before = @{}
foreach ($f in $watched) { $before[$f.FullName] = (Get-FileHash -LiteralPath $f.FullName -Algorithm SHA256).Hash }

$run = Join-Path $Work ("CE_AssetSidecarV2_" + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $run -Force | Out-Null
$scenario = Join-Path $run 'commands.txt'
$stdout = Join-Path $run 'stdout.txt'
$stderr = Join-Path $run 'stderr.txt'
@("assets.sidecar $($assets.Replace('\', '/'))", 'quit') | Set-Content -LiteralPath $scenario -Encoding UTF8

$process = Start-Process -FilePath $Exe -ArgumentList @('--script', $scenario) `
    -WorkingDirectory $root -WindowStyle Hidden `
    -RedirectStandardOutput $stdout -RedirectStandardError $stderr -PassThru
$process.WaitForExit($TimeoutSeconds * 1000) | Out-Null
if (-not $process.HasExited) { $process.Kill(); "TIMEOUT output=$run"; exit 1 }

$text = if (Test-Path -LiteralPath $stdout) { Get-Content -LiteralPath $stdout -Raw -Encoding UTF8 } else { '' }
$failures = New-Object System.Collections.Generic.List[string]

$cliPass = ([regex]::Matches($text, '\[CLI\] assets\.sidecar 통과')).Count
$summary = [regex]::Match($text, '단정 (\d+)건 중 통과 (\d+) · 실패 (\d+)')
$assertions = 0; $assertFailed = -1
if ($summary.Success) { $assertions = [int]$summary.Groups[1].Value; $assertFailed = [int]$summary.Groups[3].Value }
if ($cliPass -ne 1) { $failures.Add("[CLI] assets.sidecar 통과가 1회가 아니다: $cliPass") }
if (-not $summary.Success) { $failures.Add('단정 요약 줄이 없다') }
elseif ($assertFailed -ne 0) { $failures.Add("selftest 단정 실패 $assertFailed 건") }
elseif ($assertions -lt 80) { $failures.Add("단정 수가 너무 적다($assertions) — 검사 범위가 줄었다") }
if ($text -match 'corpus 검사 건너뜀') { $failures.Add('corpus 검사가 건너뛰어졌다 — 실자산 축이 없다') }

# corpus 줄 파싱
$models = @{}
foreach ($m in [regex]::Matches($text,
    '(?m)^\s*sidecar (\S+) ok=(\d) mat=(\d+)\(sem (\d+)/auth (\d+)\) tex=(\d+)\(sem (\d+)/auth (\d+)\) mesh=(\d+)\(sem (\d+)/auth (\d+)\) skel=(\d+) anim=(\d+)\(sem (\d+)/auth (\d+)\) issues=(\d+)')) {
    $models[$m.Groups[1].Value] = @{
        ok = [int]$m.Groups[2].Value
        mat = [int]$m.Groups[3].Value; matSem = [int]$m.Groups[4].Value; matAuth = [int]$m.Groups[5].Value
        tex = [int]$m.Groups[6].Value; texSem = [int]$m.Groups[7].Value; texAuth = [int]$m.Groups[8].Value
        mesh = [int]$m.Groups[9].Value; skel = [int]$m.Groups[12].Value; anim = [int]$m.Groups[13].Value
    }
}
$summaryLine = [regex]::Match($text, 'corpus models=(\d+) ok=(\d+) subassets=(\d+) registry=(\d+)')
if (-not $summaryLine.Success) { $failures.Add('corpus 요약 줄이 없다') }
else {
    $count = [int]$summaryLine.Groups[1].Value; $ok = [int]$summaryLine.Groups[2].Value
    if ($count -lt 11) { $failures.Add("corpus 모델이 tracked 11 미만이다: $count") }
    if ($ok -ne $count) { $failures.Add("corpus 모델 폐포 실패: ok=$ok / $count") }
    if ([int]$summaryLine.Groups[3].Value -ne [int]$summaryLine.Groups[4].Value) { $failures.Add('registry 크기 ≠ subasset 합 — 등록 거부가 있었다') }
}
if ($models.ContainsKey('Gunner_F_Mythic.glb')) {
    $g = $models['Gunner_F_Mythic.glb']
    if (-not ($g.mat -eq 2 -and $g.matSem -eq 2 -and $g.tex -eq 6 -and $g.texSem -eq 6 -and $g.skel -eq 1 -and $g.anim -ge 1)) {
        $failures.Add("Gunner 기대(재질 2 semantic·임베디드 6 semantic·skeleton 1·anim≥1)와 다르다: mat=$($g.mat)/$($g.matSem) tex=$($g.tex)/$($g.texSem) skel=$($g.skel) anim=$($g.anim)")
    }
}
if ($models.ContainsKey('scene.glb')) {
    $s = $models['scene.glb']
    if (-not ($s.mat -eq 25 -and $s.matAuth -eq 25 -and $s.matSem -eq 0)) {
        $failures.Add("scene.glb 기대(무명 재질 25 전부 authoring)와 다르다: mat=$($s.mat) sem=$($s.matSem) auth=$($s.matAuth)")
    }
}

# 원본 불변
$mutated = @()
foreach ($path in $before.Keys) {
    if (-not (Test-Path -LiteralPath $path)) { $mutated += "삭제됨: $path"; continue }
    if ((Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash -ne $before[$path]) { $mutated += "변경됨: $path" }
}
if ($mutated.Count -gt 0) { $failures.Add("원본/sidecar가 바뀌었다(이 슬라이스는 쓰지 않는다): " + ($mutated -join ', ')) }
if ($process.ExitCode -ne 0) { $failures.Add("종료 코드 $($process.ExitCode)") }

"asset-sidecar-v2 exit=$($process.ExitCode) output=$run"
"cliPass=$cliPass assertions=$assertions assertFailed=$assertFailed corpusModels=$($models.Count) $($summaryLine.Value)"
if ($failures.Count -gt 0) { '실패:'; $failures | ForEach-Object { "  $_" }; exit 1 }
"전체 통과 — epoch header·stable key·sidecar v2 단정 $assertions 건, corpus $($models.Count) 모델 폐포·registry 충돌 0, 원본 불변"
exit 0
