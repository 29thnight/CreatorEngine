# cooked catalog 제품 소비 게이트 (D5-c/D5-d)
#
# 실제 패키징과 같은 producer closure를 임시 Derived/CEMF에 굽고, catalog를
# 마운트한 Editor와 마운트하지 않은 Editor를 대조한다. 모델/텍스처뿐 아니라
# scene 문서 자체도 마운트 시 cooked artifact를 열어야 한다.
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
#   6  ★ 신선도 — artifact 하나를 소스보다 낡게 만들면 그 모델만 source로
#      간다(stale=1, cooked 하나 감소). 나머지는 그대로 cooked다 — 낡음 판정이
#      entry 단위임을 여기서 본다. 대조군(4)이 "마운트 안 함"이라면 이 축은
#      "마운트했지만 낡음"이라 서로 다른 실패를 가른다.
#
# 한계(정직): 신선도 기준은 **mtime**이다(아티팩트가 소스보다 오래되면 낡음).
# 아티팩트에 소스 시각을 넣는 길은 결정적 cook 계약이 막고 있어(ModelCookProducer
# 가 일부러 지운다) 내구적인 답은 소스 내용 해시를 CEMF에 싣는 것이고, 그것은
# 포맷 확장이라 별도 슬라이스다. 체크아웃이 소스 mtime을 새로 쓰면 신선한
# 아티팩트도 낡음으로 읽히는데, 그 방향의 오판은 source 폴백이라 안전하다.
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
    # ── 굽기: 실제 패키징과 같은 producer closure를 전부 넘긴다. 모델만 전량
    #    넘기고 shader/texture를 일부만 넘기면 PBR 재질의 GUID 의존성이 CEMF에서
    #    닫히지 않아 제품 소비 검증에 도달하기 전에 cook이 실패한다.
    $models = @(Get-ChildItem -LiteralPath $assets -File -Recurse | Where-Object {
        $_.Extension.ToLowerInvariant() -in @('.fbx', '.glb', '.gltf')
    } | Sort-Object FullName)
    if ($models.Count -eq 0) { "catalog 제품 소비를 검증할 canonical model이 없다"; exit 1 }

    # Assets/Models/*.asset은 standalone material이 아니라 legacy model cook cache다.
    $legacyModelCaches = @(Get-ChildItem -LiteralPath $assets -File -Recurse -Filter '*.asset' |
        Where-Object {
            [IO.Path]::GetRelativePath($assets, $_.FullName).Replace('\', '/') -like 'Models/*'
        })
    $legacyModelCacheNames = [Collections.Generic.HashSet[string]]::new(
        [string[]]@($legacyModelCaches | ForEach-Object { $_.FullName }),
        [StringComparer]::OrdinalIgnoreCase)

    $cookSourceRules = @(
        [pscustomobject]@{ Option = '--model';      Extensions = @('.fbx', '.glb', '.gltf') }
        [pscustomobject]@{ Option = '--texture';    Extensions = @('.png', '.hdr', '.dds') }
        [pscustomobject]@{ Option = '--shadermeta'; Extensions = @('.shadermeta') }
        [pscustomobject]@{ Option = '--material';   Extensions = @('.asset') }
        [pscustomobject]@{ Option = '--scene';      Extensions = @('.creator', '.prefab') }
    )
    $cookArgs = [Collections.Generic.List[string]]::new()
    $cookArgs.Add('--asset-root'); $cookArgs.Add($assets)
    $cookArgs.Add('--output');     $cookArgs.Add($cookOutput)
    $sourceCounts = [ordered]@{}
    foreach ($rule in $cookSourceRules) {
        $matches = @(Get-ChildItem -LiteralPath $assets -File -Recurse |
            Where-Object { $rule.Extensions -contains $_.Extension.ToLowerInvariant() } |
            Where-Object { -not $legacyModelCacheNames.Contains($_.FullName) } |
            Sort-Object FullName)
        $sourceCounts[$rule.Option] = $matches.Count
        foreach ($source in $matches) {
            $cookArgs.Add($rule.Option)
            $cookArgs.Add($source.FullName)
        }
    }
    if ($sourceCounts['--model'] -ne $models.Count) {
        "model source 열거가 어긋난다: $($sourceCounts['--model']) != $($models.Count)"
        exit 1
    }

    # resolver probe는 전체 texture closure 중 이 고정 자산을 사용한다.
    $probeTexture = Join-Path $assets 'Materials\Cube_Mat_BaseColor.png'
    if (-not (Test-Path -LiteralPath $probeTexture)) {
        "probe 텍스처가 없다: $probeTexture"; exit 1
    }

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

    if ($logOn -notmatch 'experiment\.catalog mount pass .*entries=([1-9]\d*) sources=([1-9]\d*)') {
        $fail += "1 catalog/source identity 마운트 실패 — mount 출력을 확인하라"
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
    if ($logOn -notmatch '\[scene\.document\] source=cooked guid=[0-9a-f-]{36}') {
        $fail += "3c 마운트한 scene 문서가 cooked artifact를 열지 않았다"
    }
    if ($logOn -match '\[scene\.document\] source=authoring') {
        $fail += "3d 마운트한 scene 문서가 authoring source로 폴백했다"
    }

    # ── 신선도 leg: artifact 하나를 소스보다 낡게 만든다 ──
    #    소스를 건드리지 않는다 — 작업 트리의 mtime을 바꾸면 이 게이트가 남의
    #    검사에 부작용을 남긴다. 임시 산출물만 뒤로 돌린다.
    # 씬이 실제로 쓰는 모델을 골라야 [model.dual] 계수가 움직인다. 코퍼스 14개
    # 중 씬이 여는 것은 8개뿐이라, 아무 artifact나 낡히면 계수가 그대로다
    # (첫 시도에서 실제로 그랬다). .meta의 GUID로 artifact 경로를 만든다 —
    # 경로 규약은 MakeDerivedModelArtifactPath와 같다.
    $staleSource = Join-Path $assets 'Models\Prim_Cube.glb'
    if (-not (Test-Path -LiteralPath $staleSource)) { "신선도 leg 대상이 없다: $staleSource"; exit 1 }
    $metaText = Get-Content -LiteralPath ($staleSource + '.meta') -Raw
    if ($metaText -notmatch 'guid:\s*([0-9a-fA-F-]{36})') { "meta에서 guid를 못 읽었다"; exit 1 }
    $staleGuid = $Matches[1]
    $victim = Join-Path $cookOutput ("Derived\Models\" + $staleGuid.Substring(0,2) + "\" + $staleGuid + ".cemc")
    if (-not (Test-Path -LiteralPath $victim)) { "artifact가 없다: $victim"; exit 1 }
    [IO.File]::SetLastWriteTime($victim, (Get-Date).AddDays(-30))
    $logStale = Invoke-Editor 'stale' $true
    $cookedStale = ([regex]::Matches($logStale, '\[model\.dual\] cooked 경로')).Count
    $experimentStale = ([regex]::Matches($logStale, '\[model\.dual\] experiment 경로')).Count
    if ($logStale -notmatch 'experiment\.catalog mount pass .*stale=([1-9]\d*)') {
        $fail += "6 낡은 artifact를 만들었는데 stale 계수가 0이다 — 신선도 판정이 안 돈다"
    }
    if (($cookedStale + $experimentStale) -ne ($cookedOn + $experimentOn)) {
        $fail += "6b 신선도 leg의 로드 수가 다르다 — $($cookedStale + $experimentStale) vs $($cookedOn + $experimentOn)"
    }
    # 낡은 것 하나가 source로 가고 나머지는 그대로여야 한다. 전부 source로 가면
    # 판정이 entry 단위가 아니라 표 전체를 끊은 것이다.
    if ($cookedStale -ge $cookedOn) {
        $fail += "6c 낡음 판정이 로드 경로를 바꾸지 못했다 — cooked $cookedStale (신선 $cookedOn)"
    }
    elseif ($cookedStale -lt 1) {
        $fail += "6d 낡은 하나 때문에 표 전체가 끊겼다 — cooked 0 (entry 단위가 아니다)"
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
    if ($logOff -notmatch '\[scene\.document\] source=authoring guid=[0-9a-f-]{36}') {
        $fail += "4e 대조군 scene 문서가 authoring source를 열지 않았다"
    }
    if ($logOff -match '\[scene\.document\] source=cooked') {
        $fail += "4f 마운트하지 않은 대조군이 cooked scene 문서를 열었다"
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
    "producer closure — models $($sourceCounts['--model']) · textures $($sourceCounts['--texture']) · shader metas $($sourceCounts['--shadermeta']) · materials $($sourceCounts['--material']) · scenes/prefabs $($sourceCounts['--scene'])"
    "mounted   — cooked $cookedOn · experiment $experimentOn"
    "stale     — cooked $cookedStale · experiment $experimentStale (artifact 하나 낡힘)"
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

"전체 통과 — cooked catalog가 서고, 제품이 cooked를 타며, 낡은 artifact는 entry 단위로 끊긴다"
exit 0
