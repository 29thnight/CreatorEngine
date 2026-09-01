# cooked catalog 기동 게이트 (I7-C1)
#
# 굽는 쪽은 D5-b2c에서 다 섰다(AssetCooker → Derived/ + asset-manifest.cemf →
# pak). 그런데 **읽는 쪽이 이어져 있지 않아** cooked 경로가 제품에서 한 번도
# 돌지 않았다: resolver는 nullptr catalog로 불렸고 ModelLoadRequest::cookedPath는
# 늘 비어 있었다(c3-2 실측 texCooked=0). 이 게이트가 그 배선을 실증한다.
#
# ── 판정 항목 ──
#   1  마운트 — catalog가 실제 CEMF에서 서고 entry가 0이 아니다
#   2  신원 맞물림 — 씬의 모델 GUID가 cooked artifact로 해석된다
#      (표만 서고 제품 신원과 안 맞으면 cookedPath는 여전히 빈 경로다)
#   3  ★ 제품 소비 — 마운트 뒤 로드가 **cooked 경로로 그린다**
#      ([model.dual] cooked 경로 ≥ 1). 1·2가 있어도 디코더가 거부하면 조용히
#      source로 폴백하므로, 이 축만이 "cooked가 실제로 돌았다"를 말한다.
#   4  대조군 — 마운트 없이 같은 씬을 로드하면 cooked 경로가 **0**이다.
#      이것이 없으면 3이 마운트 덕인지 원래 그런지 갈리지 않는다.
#
# 한계(정직): 쿠킹 산출물의 **신선도**(cooked가 source보다 낡았는가)는 아직
# 판정하지 않는다 — I7의 남은 항목이다. 이 게이트는 갓 구운 트리를 쓴다.
param(
    [string]$Exe = "",
    [string]$AssetCooker = "",
    [string]$Scene = "",
    [string]$Work = $env:TEMP,
    [int]$TimeoutSec = 300
)

$ErrorActionPreference = 'Stop'
$repoRoot = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot "..\.."))

if ([string]::IsNullOrEmpty($Exe)) {
    $Exe = Join-Path $repoRoot "Bin\x64-Debug\Editor\CreatorEditor.exe"
}
if (-not (Test-Path $Exe)) { "실행 파일이 없다: $Exe"; exit 1 }
$Exe = (Resolve-Path -LiteralPath $Exe).Path

if ([string]::IsNullOrEmpty($AssetCooker)) {
    $AssetCooker = Join-Path $repoRoot "Bin\x64-Debug\Tools\AssetCooker\AssetCooker.exe"
}
if (-not (Test-Path $AssetCooker)) { "AssetCooker가 없다: $AssetCooker"; exit 1 }

if ([string]::IsNullOrEmpty($Scene)) {
    $Scene = Join-Path $repoRoot "Dynamic_CPP\Assets\Scenes\FT_Primitives.creator"
}
if (-not (Test-Path $Scene)) { "씬이 없다: $Scene"; exit 1 }
$Scene = (Resolve-Path -LiteralPath $Scene).Path

$assets = (Resolve-Path (Join-Path $repoRoot 'Dynamic_CPP\Assets')).Path
$run = Join-Path $Work ("CE_CookedCatalog_" + [Guid]::NewGuid().ToString('N'))
$cookOutput = Join-Path $run 'cooked'
New-Item -ItemType Directory -Path $run -Force | Out-Null

$fail = @()
try {
    # ── 굽기: 씬이 실제로 쓰는 모델만 구우면 신원 맞물림 판정이 좁아진다.
    #    코퍼스 전량을 굽는다(cook-all과 같은 입력).
    $models = @(Get-ChildItem -LiteralPath $assets -File -Recurse | Where-Object {
        $_.Extension.ToLowerInvariant() -in @('.fbx', '.glb', '.gltf')
    } | Sort-Object FullName)
    $gbuffer = Join-Path $assets 'Shaders\DefaultPassShader\GBuffer.shadermeta'
    $cookArgs = [Collections.Generic.List[string]]::new()
    $cookArgs.Add('--asset-root'); $cookArgs.Add($assets)
    $cookArgs.Add('--output');     $cookArgs.Add($cookOutput)
    foreach ($model in $models) { $cookArgs.Add('--model'); $cookArgs.Add($model.FullName) }
    $cookArgs.Add('--shadermeta'); $cookArgs.Add($gbuffer)
    # ★ texture를 하나 굽는다 — 이것이 없으면 resolver의 cooked 소비 축이
    #   "cooked 후보가 없어서 source로 갔다"와 "배선이 끊겼다"를 못 가른다.
    $probeTexture = Join-Path $assets 'Materials\Cube_Mat_BaseColor.png'
    if (-not (Test-Path -LiteralPath $probeTexture)) {
        "probe 텍스처가 없다: $probeTexture"; exit 1
    }
    $cookArgs.Add('--texture'); $cookArgs.Add($probeTexture)

    $cookOut = Join-Path $run 'cook.out.log'
    $cookErr = Join-Path $run 'cook.err.log'
    $cooker = Start-Process -FilePath $AssetCooker -ArgumentList $cookArgs.ToArray() `
        -WorkingDirectory $repoRoot -WindowStyle Hidden -PassThru `
        -RedirectStandardOutput $cookOut -RedirectStandardError $cookErr
    if (-not $cooker.WaitForExit($TimeoutSec * 1000)) {
        Stop-Process -Id $cooker.Id -Force -ErrorAction SilentlyContinue
        "AssetCooker가 $TimeoutSec 초 안에 끝나지 않았다."
        exit 1
    }
    if (0 -ne $cooker.ExitCode) {
        "AssetCooker 실패 exit=$($cooker.ExitCode)"
        Get-Content -LiteralPath $cookErr -Raw
        exit 1
    }
    $manifest = Join-Path $cookOutput 'Derived\asset-manifest.cemf'
    if (-not (Test-Path -LiteralPath $manifest)) {
        "CEMF가 만들어지지 않았다: $manifest"; exit 1
    }

    function Invoke-Editor([string]$label, [bool]$mount) {
        $scenario = Join-Path $run "scenario-$label.txt"
        $lines = [Collections.Generic.List[string]]::new()
        # ★ 마운트가 **씬 로드보다 먼저**여야 한다 — 모델은 로드 시점에
        #   cookedPath를 정하고, 이후에는 캐시라 다시 정하지 않는다.
        if ($mount) { $lines.Add("experiment.catalog mount $cookOutput"); $lines.Add("wait 10") }
        $lines.Add("scene.switch " + $Scene.Replace('\', '/'))
        $lines.Add("wait 120")
        $lines.Add("experiment.catalog")
        $lines.Add("wait 10")
        $lines.Add("experiment.catalog probe $probeTexture")
        $lines.Add("wait 10")
        $lines.Add("quit")
        Set-Content -LiteralPath $scenario -Value $lines -Encoding UTF8

        $stdout = Join-Path $run "$label.out.log"
        $stderr = Join-Path $run "$label.err.log"
        $proc = Start-Process -FilePath $Exe -ArgumentList @("--script", $scenario) `
            -WorkingDirectory $repoRoot -WindowStyle Hidden -PassThru `
            -RedirectStandardOutput $stdout -RedirectStandardError $stderr
        if (-not $proc.WaitForExit($TimeoutSec * 1000)) {
            Stop-Process -Id $proc.Id -Force -ErrorAction SilentlyContinue
            throw "[$label] $TimeoutSec 초 안에 끝나지 않았다."
        }
        return Get-Content -LiteralPath $stdout -Raw
    }

    $logOn = Invoke-Editor 'mounted' $true
    $cookedOn = ([regex]::Matches($logOn, '\[model\.dual\] cooked 경로')).Count
    $experimentOn = ([regex]::Matches($logOn, '\[model\.dual\] experiment 경로')).Count

    if ($logOn -notmatch 'experiment\.catalog mount pass .*entries=([1-9]\d*)') {
        $fail += "1 catalog 마운트 실패 — mount 출력을 확인하라"
    }
    if ($logOn -notmatch 'experiment\.catalog pass .*models=([1-9]\d*)') {
        $fail += "1b catalog에 model entry가 0이다"
    }
    if ($logOn -notmatch 'experiment\.catalog pass .*modelsProbed=([1-9]\d*) modelsResolved=([1-9]\d*)') {
        $fail += "2 씬 모델 GUID가 cooked artifact로 해석되지 않았다 — 신원이 안 맞물린다"
    }
    if ($cookedOn -lt 1) {
        $fail += "3 cooked 경로 로드가 0건 — 표는 섰는데 제품이 cooked를 타지 않았다"
    }
    # ★ 3b — resolver(sealing이 매 프레임 타는 그 경로)가 cooked artifact를
    #   실제로 골랐는가. 모델 축(3)과 독립이다: 모델은 cookedPath로, 텍스처는
    #   catalog services로 간다.
    if ($logOn -notmatch 'experiment\.catalog probe pass .*cooked=([1-9]\d*)') {
        $fail += "3b resolver가 cooked texture를 고르지 않았다 — services 배선이 끊겼다"
    }

    $logOff = Invoke-Editor 'unmounted' $false
    $cookedOff = ([regex]::Matches($logOff, '\[model\.dual\] cooked 경로')).Count
    $experimentOff = ([regex]::Matches($logOff, '\[model\.dual\] experiment 경로')).Count
    if ($logOff -notmatch 'experiment\.catalog skip') {
        $fail += "4 대조군인데 catalog가 서 있다 — 저작 트리에 Derived가 생겼는가"
    }
    if ($cookedOff -ne 0) {
        $fail += "4b 마운트 없이 cooked 경로 $cookedOff 건 — 대조군이 성립하지 않는다"
    }
    if ($logOff -notmatch 'experiment\.catalog probe pass .*catalog=0 .*cooked=0 sourceFallback=([1-9]\d*)') {
        $fail += "4d 대조군인데 resolver가 cooked를 골랐거나 해석 자체가 실패했다"
    }
    if ($experimentOff -lt 1) {
        $fail += "4c 대조군에서 experiment 경로가 0건 — 로드 자체가 안 돌았다"
    }
    # 같은 씬이므로 로드한 모델 수는 두 실행이 같아야 한다. 마운트가 모델을
    # 통째로 떨구면(cooked 거부→폴백도 실패) 위 축들이 초록인 채 여기서 갈린다.
    if (($cookedOn + $experimentOn) -ne ($cookedOff + $experimentOff)) {
        $fail += "5 로드한 모델 수가 다르다 — 마운트 $($cookedOn + $experimentOn) vs 대조군 $($cookedOff + $experimentOff)"
    }

    $probeOn = ([regex]::Match($logOn, 'experiment\.catalog probe [^

]*')).Value
    $probeOff = ([regex]::Match($logOff, 'experiment\.catalog probe [^

]*')).Value
    "probe(mounted)   — $probeOn"
    "probe(unmounted) — $probeOff"
    "mounted   — cooked $cookedOn · experiment $experimentOn"
    "unmounted — cooked $cookedOff · experiment $experimentOff (기대: 0 · $($cookedOn + $experimentOn))"
}
finally {
    Remove-Item -LiteralPath $run -Recurse -Force -ErrorAction SilentlyContinue
}

if ($fail.Count -gt 0) {
    ""
    "실패 $($fail.Count) 건:"
    $fail | ForEach-Object { "  [실패] $_" }
    exit 1
}

"전체 통과 — cooked catalog가 서고 제품 모델 로드가 cooked artifact를 탄다"
exit 0
