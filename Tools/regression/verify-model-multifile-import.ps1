# 다중 파일 `.gltf` 임포트 — `model.load`가 사이드카를 구조째 옮기는가.
#
# ── 이 게이트가 메우는 구멍 ──
#
# `IsAllowedImportExtension`은 `.gltf`를 허용하는데 `ImportSourceAsset`는
# `destinationDirectory / source.filename()`로 **파일 하나만** 복사했다. 결과:
# 하위 폴더가 평탄화되고 `.bin`·이미지가 따라오지 않아 `buffers[].uri` ·
# `images[].uri`가 새 위치에서 풀리지 않는다 — 받아들이는 척하고 깨진 자산을
# 만든다(2026-09-14 실측: `model.import_failed` 종료 코드 4, 그리고 평탄화된
# `.gltf` 사본 하나가 자산 트리에 잔해로 남았다).
#
# 임포터 자체는 멀쩡하다. `GltfImporter`가 `baseDirectory`를 넘기고 `sources::URI`
# 분기가 이미지 경로를 만든다. 깨진 것은 **복사 단계뿐**이었다.
#
# ── fixture를 왜 새로 만들었나 ──
#
# `Dynamic_CPP/Assets/Models/TextureSettingsTest/`(Khronos 표본)이 이 결함의
# 재현체지만 `.gitignore`의 `/Dynamic_CPP/Assets/Models/*`에 막혀 **추적 밖**이다.
# 그것만 쓰면 clean checkout에서 게이트가 조용히 비어 버린다. 그래서 저장소가
# 소유하는 최소 fixture(`fixtures/gltf-multifile/`, 손으로 만든 쿼드 1 + 외부
# `.bin` + **하위 폴더**의 PNG 1, 그리고 같은 내용을 data: URI 로 품은 embedded
# 판본 하나)를 핵심 축으로 쓰고, Khronos 표본은 **있을 때만** 제자리 임포트
# 축(S2)으로 덧붙인다. 건너뛰면 요약에 그렇게 적는다.
#
# ★ 추적 자산(`Prim_Cube.glb` 등)은 축에 쓰지 않는다. 재임포트가 sidecar 의
#   `generation` 을 올려 게이트를 돌릴 때마다 추적 파일이 더러워진다.
#
# ★ 하위 폴더(`Textures/Probe.png`)가 핵심이다. 사이드카를 평면으로 복사해도
#   `.bin`은 우연히 맞지만 하위 폴더 이미지는 반드시 깨진다.
#
# ── 판정 항목 ──
#
#   S1  실제 복사    임시 폴더의 다중 파일 glTF를 model.load → 성공하고
#                    Models/<stem>/ 에 gltf·bin·Textures/Probe.png 가 구조째 놓인다.
#                    평탄화 사본(Models/<stem>.gltf · Models/Probe.png) 0.
#   S1b 텍스처 해석  복사한 PNG에 .meta 가 생기고, cooked generation(model.cemc)이
#                    그 texture assetId 를 담는다. ★ assets.scenemodel 의
#                    generationTextures 는 **임베디드** 텍스처만 센다 — 외부 URI
#                    텍스처는 그 축에 잡히지 않아 여기서는 쓸 수 없다.
#   S2  제자리 임포트 원본이 이미 자산 트리 안이면 복사 없이 성공하고 .meta 의
#                    assetId 가 그대로다(재발급 0). fixture 가 없으면 명시적 건너뜀.
#   S3  거부         `..` 탈출(퍼센트 인코딩 위장 포함) · http:// 원격 · 없는
#                    사이드카를 각각 거부하고 자산 트리에 아무것도 남기지 않는다.
#   S3b 되돌리기     복사 **도중** 실패하면 이미 옮긴 것을 거둔다. 목적지의 사이드카
#                    자리를 폴더로 막아 copy_file 을 실패시켜 잰다.
#                    ★ S3 의 거부 넷은 전부 복사 **전에** 끊기므로 되돌리기를
#                      자극하지 못한다(변이로 확인) — 이 축이 따로 있어야 한다.
#                    ★ 자연 입력으로는 안 된다: 잘린 `.bin` 도 깨진 PNG 도 임포트가
#                      **성공**한다(실측). 그 관대함은 이 게이트의 소관이 아니다.
#   S4  회귀 없음    data: URI 로 전부 품은 embedded glTF 는 오늘처럼 Models/ 평면에
#                    놓이고 전용 폴더가 생기지 않으며, 재임포트가 신원을 재발급하지
#                    않는다. ★ data: URI 는 거부 대상이 아니라 **건너뛸** 대상이다 —
#                    거부로 바꾸면 embedded glTF 가 통째로 죽는다.
#   S5  정적         훑기의 fastgltf 확장 집합이 GltfImporter 와 같다. 어긋나면
#                    extensionsRequired 를 쓴 모델이 훑기 단계에서만 거부된다.
param(
    [string]$Editor = (Join-Path $PSScriptRoot '..\..\Bin\x64-Debug\Editor\CreatorEditor.exe'),
    [string]$Work = $env:TEMP,
    [int]$TimeoutSeconds = 300
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'CommandResults.ps1')

$root = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
$assetModels = Join-Path $root 'Dynamic_CPP\Assets\Models'
$run = Join-Path $Work ('creator-gltf-multifile-' + [guid]::NewGuid().ToString('N'))
$failures = [System.Collections.Generic.List[string]]::new()
$notes = [System.Collections.Generic.List[string]]::new()
function Add-Failure([string]$Message) { $failures.Add($Message) }

# 임포트 목적지는 실제 자산 트리다. 이름을 실행마다 고유하게 만들면 잔해 판정이
# 흐려지므로 고정 이름을 쓰고, 시작과 끝에서 반드시 지운다.
$probeStem = 'GltfMultiFileProbe'
$probeBundle = Join-Path $assetModels $probeStem

function Remove-ProbeArtifacts {
    foreach ($path in @(
            $probeBundle,
            (Join-Path $assetModels "$probeStem.gltf"),
            (Join-Path $assetModels "$probeStem.gltf.meta"),
            (Join-Path $assetModels "$probeStem.bin"),
            (Join-Path $assetModels 'Probe.png'),
            (Join-Path $assetModels 'Probe.png.meta'))) {
        Remove-Item -LiteralPath $path -Recurse -Force -ErrorAction SilentlyContinue
    }
}

function Invoke-Editor([string]$Label, [string[]]$Commands) {
    $scenario = Join-Path $run ($Label + '.commands.txt')
    [IO.File]::WriteAllText($scenario, ($Commands -join "`n") + "`n",
        [Text.UTF8Encoding]::new($false))
    $resultPath = Join-Path $run ($Label + '.results.jsonl')
    $start = [Diagnostics.ProcessStartInfo]::new()
    $start.FileName = $Editor
    $start.Arguments = '--commandlet-script "' + $scenario.Replace('"', '\"') + '"' +
        ' --result-file "' + $resultPath + '"'
    $start.WorkingDirectory = $root
    $start.UseShellExecute = $false
    $start.CreateNoWindow = $true
    $start.RedirectStandardOutput = $true
    $start.RedirectStandardError = $true
    $process = [Diagnostics.Process]::new()
    $process.StartInfo = $start
    if (-not $process.Start()) { throw "CreatorEditor 시작 실패: $Editor" }
    $stdoutTask = $process.StandardOutput.ReadToEndAsync()
    $stderrTask = $process.StandardError.ReadToEndAsync()
    if (-not $process.WaitForExit($TimeoutSeconds * 1000)) {
        $process.Kill()
        throw "CreatorEditor timeout($Label): $run"
    }
    $process.WaitForExit()
    $stdout = $stdoutTask.GetAwaiter().GetResult()
    $stderr = $stderrTask.GetAwaiter().GetResult()
    [IO.File]::WriteAllText((Join-Path $run ($Label + '.stdout.txt')), $stdout,
        [Text.UTF8Encoding]::new($false))
    [IO.File]::WriteAllText((Join-Path $run ($Label + '.stderr.txt')), $stderr,
        [Text.UTF8Encoding]::new($false))
    return [pscustomobject]@{
        Results  = @(Read-CommandResults $resultPath)
        Stdout   = $stdout
        Stderr   = $stderr
        ExitCode = $process.ExitCode
    }
}

# 사이드카를 고쳐 심은 fixture 사본을 만든다. UriRewrite 는 (원문 → 대체) 표.
#
# PlantOutside: 원본 폴더 **밖**(스테이지의 부모)에 같은 PNG 를 심는다. 탈출
# 케이스에 반드시 필요하다 — 심지 않으면 `../Probe.png` 가 가리키는 파일이
# 애초에 없어서 "탈출이라 거부"가 아니라 "없어서 거부"로 붉어진다. 그러면
# 경로 봉쇄를 걷어내는 변이를 자극하지 못한다.
#
function New-StagedFixture([string]$Name, [hashtable]$UriRewrite, [string[]]$Drop,
    [switch]$PlantOutside) {
    $source = Join-Path $PSScriptRoot 'fixtures\gltf-multifile'
    $stage = Join-Path $run $Name
    Copy-Item -LiteralPath $source -Destination $stage -Recurse -Force
    Rename-Item -LiteralPath (Join-Path $stage 'MultiFileProbe.gltf') -NewName "$probeStem.gltf"
    Rename-Item -LiteralPath (Join-Path $stage 'MultiFileProbe.bin') -NewName "$probeStem.bin"
    $gltf = Join-Path $stage "$probeStem.gltf"
    $text = [IO.File]::ReadAllText($gltf)
    $text = $text.Replace('MultiFileProbe.bin', "$probeStem.bin")
    foreach ($key in $UriRewrite.Keys) { $text = $text.Replace($key, $UriRewrite[$key]) }
    [IO.File]::WriteAllText($gltf, $text, [Text.UTF8Encoding]::new($false))
    if ($PlantOutside) {
        Copy-Item -LiteralPath (Join-Path $stage 'Textures\Probe.png') `
            -Destination (Join-Path $run 'Probe.png') -Force
    }
    foreach ($relative in $Drop) {
        Remove-Item -LiteralPath (Join-Path $stage $relative) -Force
    }
    return $gltf.Replace('\', '/')
}

# Get-SucceededCommand 는 throw 라서 한 축이 무너지면 catch 로 빠져 **뒤의 축이
# 전부 실행되지 않는다**. 그러면 "거부가 살아 있는가"를 못 재고도 게이트는
# 붉으니 고쳐진 것처럼 보인다(2026-09-14 변이에서 실제로 그랬다 — S1 하나가
# S3·S4·S5 를 통째로 가렸다). 여기서는 실패를 적고 $null 을 돌려준다.
function Read-SucceededData($Results, [string]$Command, [string]$Axis) {
    $rows = @($Results | Where-Object command -eq $Command)
    if ($rows.Count -ne 1) {
        Add-Failure "$Axis $Command 결과가 1건이 아니다: $($rows.Count)"
        return $null
    }
    if ($rows[0].status -ne 'succeeded') {
        Add-Failure "$Axis $Command status=$($rows[0].status) code=$($rows[0].code): $($rows[0].message)"
        return $null
    }
    return $rows[0].data
}

function Get-MetaGuid([string]$Path) {
    if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) { return '' }
    foreach ($line in [IO.File]::ReadAllLines($Path)) {
        if ($line -match '^\s*(assetId|guid):\s*([0-9a-fA-F-]{36})\s*$') { return $Matches[2] }
    }
    return ''
}

function Get-SidecarField([string]$Path, [string]$Field) {
    if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) { return '' }
    foreach ($line in [IO.File]::ReadAllLines($Path)) {
        if ($line -match ('^\s*' + [regex]::Escape($Field) + ':\s*(.+?)\s*$')) { return $Matches[1] }
    }
    return ''
}

# cooked 모델이 외부 texture 의 assetId 16바이트를 담고 있는가.
function Test-CookedContainsGuid([string]$CookedPath, [string]$Guid) {
    $needle = [byte[]]::new(16)
    $hex = $Guid.Replace('-', '')
    for ($i = 0; $i -lt 16; $i++) {
        $needle[$i] = [Convert]::ToByte($hex.Substring($i * 2, 2), 16)
    }
    $blob = [IO.File]::ReadAllBytes($CookedPath)
    for ($i = 0; $i -le $blob.Length - 16; $i++) {
        $match = $true
        for ($j = 0; $j -lt 16; $j++) {
            if ($blob[$i + $j] -ne $needle[$j]) { $match = $false; break }
        }
        if ($match) { return $true }
    }
    return $false
}

try {
    if (-not (Test-Path -LiteralPath $Editor -PathType Leaf)) {
        throw "CreatorEditor 실행 파일이 없다: $Editor"
    }
    $fixture = Join-Path $PSScriptRoot 'fixtures\gltf-multifile\MultiFileProbe.gltf'
    if (-not (Test-Path -LiteralPath $fixture -PathType Leaf)) {
        throw "다중 파일 glTF fixture가 없다: $fixture"
    }
    New-Item -ItemType Directory -Path $run -Force | Out-Null
    if ($run -match '\s') { throw "console 인자로 넘길 작업 경로에 공백이 있다: $run" }
    Remove-ProbeArtifacts

    # ── S1 · S1b: 자산 트리 밖 다중 파일 원본을 실제로 복사한다 ──
    $staged = New-StagedFixture 'ok' @{} @()
    $import = Invoke-Editor 'import' @("model.load $staged", 'wait 60', 'quit')
    if ($import.ExitCode -ne 0) { Add-Failure "S1 종료 코드 $($import.ExitCode)" }
    if (-not [string]::IsNullOrWhiteSpace($import.Stderr)) { Add-Failure 'S1 stderr가 비어 있지 않다.' }
    $imported = Read-SucceededData $import.Results 'model.load' 'S1'

    $expectedGltf = Join-Path $probeBundle "$probeStem.gltf"
    if ($null -ne $imported -and $imported.path -ne $expectedGltf) {
        Add-Failure "S1 임포트 목적지가 전용 폴더가 아니다: $($imported.path)"
    }
    foreach ($relative in @("$probeStem.gltf", "$probeStem.bin", 'Textures\Probe.png')) {
        if (-not (Test-Path -LiteralPath (Join-Path $probeBundle $relative) -PathType Leaf)) {
            Add-Failure "S1 사이드카가 구조째 옮겨지지 않았다: $relative"
        }
    }
    # 평탄화 잔해가 남으면 옛 경로가 되살아난 것이다.
    foreach ($flattened in @("$probeStem.gltf", "$probeStem.bin", 'Probe.png')) {
        if (Test-Path -LiteralPath (Join-Path $assetModels $flattened)) {
            Add-Failure "S1 평탄화 사본이 자산 루트에 남았다: $flattened"
        }
    }

    $textureGuid = Get-MetaGuid (Join-Path $probeBundle 'Textures\Probe.png.meta')
    if ([string]::IsNullOrWhiteSpace($textureGuid)) {
        Add-Failure 'S1b 복사한 texture에 .meta identity가 없다(모델 저작이 이것을 읽는다).'
    }
    $modelSidecar = Join-Path $probeBundle "$probeStem.gltf.meta"
    $modelAssetId = Get-MetaGuid $modelSidecar
    $generation = Get-SidecarField $modelSidecar 'generation'
    $cooked = if ([string]::IsNullOrWhiteSpace($modelAssetId) -or [string]::IsNullOrWhiteSpace($generation)) { '' }
    else { Join-Path $root "Dynamic_CPP\Library\ModelAssetGenerations\$modelAssetId\$generation\model.cemc" }
    if ([string]::IsNullOrWhiteSpace($cooked) -or -not (Test-Path -LiteralPath $cooked -PathType Leaf)) {
        Add-Failure "S1b cooked generation이 없다: assetId=$modelAssetId generation=$generation"
    }
    elseif (-not [string]::IsNullOrWhiteSpace($textureGuid)) {
        # 외부 URI texture 는 cooked 모델에 assetId 16바이트로 실린다. 이 결합이
        # 곧 "상대 URI가 새 위치에서 풀렸다"의 직접 증거다.
        if (-not (Test-CookedContainsGuid $cooked $textureGuid)) {
            Add-Failure "S1b cooked 모델이 외부 texture assetId($textureGuid)를 담지 않는다."
        }
    }
    Remove-ProbeArtifacts

    # ── S3: 신뢰 경계 밖 참조는 거부하고 잔해를 남기지 않는다 ──
    $rejections = @(
        @{ Label = 'escape'; Rewrite = @{ '"Textures/Probe.png"' = '"../Probe.png"' }; Drop = @(); Plant = $true },
        @{ Label = 'escapepercent'; Rewrite = @{ '"Textures/Probe.png"' = '"%2e%2e/Probe.png"' }; Drop = @(); Plant = $true },
        @{ Label = 'remote'; Rewrite = @{ '"Textures/Probe.png"' = '"http://example.invalid/x.png"' }; Drop = @(); Plant = $false },
        @{ Label = 'missing'; Rewrite = @{}; Drop = @('Textures/Probe.png'); Plant = $false }
    )
    foreach ($case in $rejections) {
        $stagedBad = New-StagedFixture $case.Label $case.Rewrite $case.Drop `
            -PlantOutside:$case.Plant
        $bad = Invoke-Editor $case.Label @("model.load $stagedBad", 'wait 30', 'quit')
        $rows = @($bad.Results | Where-Object command -eq 'model.load')
        if ($rows.Count -ne 1) {
            Add-Failure "S3($($case.Label)) model.load 결과가 1건이 아니다: $($rows.Count)"
            Remove-ProbeArtifacts
            continue
        }
        $row = $rows[0]
        if ($row.status -ne 'failed' -or $row.code -ne 'model.import_failed') {
            Add-Failure "S3($($case.Label)) 거부하지 않았다: status=$($row.status) code=$($row.code)"
        }
        if ($bad.ExitCode -eq 0) { Add-Failure "S3($($case.Label)) 종료 코드가 0이다." }
        if (Test-Path -LiteralPath $probeBundle) {
            $leftovers = @(Get-ChildItem -LiteralPath $probeBundle -Recurse -Force -File)
            if ($leftovers.Count -gt 0) {
                Add-Failure "S3($($case.Label)) 실패가 잔해를 남겼다: $($leftovers.Count)개"
            }
        }
        foreach ($flattened in @("$probeStem.gltf", 'Probe.png')) {
            if (Test-Path -LiteralPath (Join-Path $assetModels $flattened)) {
                Add-Failure "S3($($case.Label)) 평탄화 잔해가 남았다: $flattened"
            }
        }
        Remove-ProbeArtifacts
    }

    # ── S3b: 복사 도중 실패하면 이미 옮긴 것을 거둔다 ──
    #
    # 위 거부 넷은 전부 복사 **전에** 끊겨서 되돌리기를 자극하지 못한다(변이로
    # 확인했다). 자연 입력으로도 안 된다 — 잘린 `.bin` 도 깨진 PNG 도 임포트가
    # 성공한다(실측). 그래서 목적지의 사이드카 자리를 **폴더로 막아** copy_file 을
    # 실패시킨다. `.gltf` 는 이미 복사된 뒤이므로, 되돌리기가 없으면 그것이 잔해로
    # 남는다.
    $blockedStage = New-StagedFixture 'blocked' @{} @()
    New-Item -ItemType Directory -Force -Path (Join-Path $probeBundle 'Textures\Probe.png') | Out-Null
    $blocked = Invoke-Editor 'blocked' @("model.load $blockedStage", 'wait 30', 'quit')
    $blockedRows = @($blocked.Results | Where-Object command -eq 'model.load')
    if ($blockedRows.Count -ne 1 -or $blockedRows[0].status -ne 'failed') {
        Add-Failure "S3b 막힌 목적지인데 실패하지 않았다: $($blockedRows[0].status)"
    }
    $blockedLeftovers = @(Get-ChildItem -LiteralPath $probeBundle -Recurse -Force -File `
            -ErrorAction SilentlyContinue)
    if ($blockedLeftovers.Count -gt 0) {
        Add-Failure ('S3b 복사 도중 실패가 잔해를 남겼다: ' +
            (($blockedLeftovers | ForEach-Object { $_.Name }) -join ', '))
    }
    Remove-ProbeArtifacts

    # ── S4: 단일 파일(embedded) glTF는 오늘 그대로 평면에 놓인다 ──
    #
    # data: URI 로 버퍼·이미지를 품은 fixture 를 쓴다. 두 가지를 한 번에 잰다.
    #   (1) 사이드카가 없으면 전용 폴더를 만들지 않는다(기존 동작 회귀 없음).
    #   (2) data: URI 는 **거부 대상이 아니라 건너뛸 대상**이다 — 옮길 파일이
    #       없을 뿐이다. 이것을 거부로 바꾸면 embedded glTF 가 통째로 죽는다.
    #
    # ★ 여기에 추적 자산(Prim_Cube.glb)을 쓰지 않는다. 재임포트가 sidecar 의
    #   `generation` 을 올려 **게이트를 돌릴 때마다 추적 파일이 더러워진다**
    #   (처음에 그렇게 썼다가 7 → 15 로 밀린 것을 보고 바꿨다).
    $embedded = Join-Path $run 'embedded'
    New-Item -ItemType Directory -Path $embedded -Force | Out-Null
    $embeddedSource = Join-Path $embedded "$probeStem.gltf"
    Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'fixtures\gltf-multifile\SingleFileProbe.gltf') `
        -Destination $embeddedSource -Force
    $single = Invoke-Editor 'single' @(
        "model.load $($embeddedSource.Replace('\', '/'))", 'wait 30', 'quit')
    $singleData = Read-SucceededData $single.Results 'model.load' 'S4'
    $flatDestination = Join-Path $assetModels "$probeStem.gltf"
    if ($null -ne $singleData -and $singleData.path -ne $flatDestination) {
        Add-Failure "S4 embedded glTF 목적지가 평면이 아니다: $($singleData.path)"
    }
    if (Test-Path -LiteralPath $probeBundle) {
        Add-Failure 'S4 사이드카가 없는데 전용 폴더가 생겼다.'
    }
    # 같은 원본을 다시 임포트해도 신원은 그대로여야 한다(재발급 0).
    $firstId = Get-MetaGuid ($flatDestination + '.meta')
    if ([string]::IsNullOrWhiteSpace($firstId)) {
        Add-Failure 'S4 embedded glTF 에 sidecar identity 가 없다.'
    }
    $again = Invoke-Editor 'single2' @(
        "model.load $($embeddedSource.Replace('\', '/'))", 'wait 30', 'quit')
    $null = Read-SucceededData $again.Results 'model.load' 'S4(재임포트)'
    $secondId = Get-MetaGuid ($flatDestination + '.meta')
    if ($firstId -ne $secondId) {
        Add-Failure "S4 재임포트가 모델 신원을 재발급했다: $firstId -> $secondId"
    }
    Remove-ProbeArtifacts

    # ── S2: 제자리 임포트(Khronos 표본, 추적 밖이라 있을 때만) ──
    $khronos = Join-Path $assetModels 'TextureSettingsTest\TextureSettingsTest.gltf'
    if (Test-Path -LiteralPath $khronos -PathType Leaf) {
        $beforeId = Get-MetaGuid ($khronos + '.meta')
        $inPlace = Invoke-Editor 'inplace' @("model.load $($khronos.Replace('\', '/'))", 'wait 30', 'quit')
        $inPlaceData = Read-SucceededData $inPlace.Results 'model.load' 'S2'
        if ($null -ne $inPlaceData -and $inPlaceData.path -ne $khronos) {
            Add-Failure "S2 제자리 임포트가 경로를 옮겼다: $($inPlaceData.path)"
        }
        if (Test-Path -LiteralPath (Join-Path $assetModels 'TextureSettingsTest.gltf')) {
            Add-Failure 'S2 평탄화 사본이 남았다: TextureSettingsTest.gltf'
        }
        $afterId = Get-MetaGuid ($khronos + '.meta')
        if ($beforeId -ne $afterId) {
            Add-Failure "S2 제자리 임포트가 모델 신원을 재발급했다: $beforeId -> $afterId"
        }
    }
    else {
        $notes.Add('S2 건너뜀 — Dynamic_CPP/Assets/Models/TextureSettingsTest 없음(gitignore로 추적 밖)')
    }

    # ── S5: 훑기와 임포터의 fastgltf 확장 집합이 같은가 ──
    $scanSource = [IO.File]::ReadAllText(
        (Join-Path $root 'Engine\RenderEngine\Experiment\Import\GltfSourceDependencies.cpp'))
    $importerSource = [IO.File]::ReadAllText(
        (Join-Path $root 'Engine\RenderEngine\Experiment\Import\GltfImporter.cpp'))
    $pattern = 'fastgltf::Extensions::[A-Za-z_]+'
    $scanExtensions = @([regex]::Matches($scanSource, $pattern) | ForEach-Object { $_.Value } | Sort-Object -Unique)
    $importerExtensions = @([regex]::Matches($importerSource, $pattern) | ForEach-Object { $_.Value } | Sort-Object -Unique)
    if ($scanExtensions.Count -eq 0 -or $importerExtensions.Count -eq 0) {
        Add-Failure 'S5 확장 집합을 어느 한쪽에서도 찾지 못했다(대조가 빈 집합이 됐다).'
    }
    elseif (($scanExtensions -join ',') -ne ($importerExtensions -join ',')) {
        Add-Failure ('S5 훑기와 임포터의 확장 집합이 다르다: scan=[' +
            ($scanExtensions -join ',') + '] importer=[' + ($importerExtensions -join ',') + ']')
    }

    # ── 정적: 파일 하나만 복사하던 옛 모양이 되살아났는가 ──
    $database = [IO.File]::ReadAllText((Join-Path $root 'Editor\EngineEntry\EditorAssetDatabase.cpp'))
    if ($database -notmatch 'ScanGltfSourceDependencies') {
        Add-Failure '정적 임포트가 glTF 사이드카 폐포를 훑지 않는다.'
    }
    if ($database -notmatch 'CopyImportedFile\(sourceDirectory / relative') {
        Add-Failure '정적 임포트가 사이드카를 복사하지 않는다.'
    }
}
catch {
    Add-Failure "예외: $($_.Exception.Message)"
}
finally {
    Remove-ProbeArtifacts
}

$summary = "model-multifile-import failures=$($failures.Count) run=$run"
if ($failures.Count -gt 0) {
    $summary
    $failures | ForEach-Object { "  $_" }
    $notes | ForEach-Object { "  note: $_" }
    exit 1
}
$summary
$notes | ForEach-Object { "  note: $_" }
'통과 — 다중 파일 glTF가 폴더 구조째 임포트되고 외부 texture가 cooked 모델에 실리며, 탈출/원격/누락 참조는 잔해 없이 거부된다'
Remove-Item -LiteralPath $run -Recurse -Force -ErrorAction SilentlyContinue
exit 0
