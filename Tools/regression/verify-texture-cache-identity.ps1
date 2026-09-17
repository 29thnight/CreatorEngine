[CmdletBinding()]
param(
    [string]$Exe = (Join-Path $PSScriptRoot '../../Bin/x64-Release/Editor/CreatorEditor.exe'),
    [string]$Work = (Join-Path $env:TEMP 'creator-texture-cache-identity')
)
# ModelGeometryTextureImprovementPlan G2 — 텍스처 캐시의 신원.
#
# `DataSystem::LoadSharedTexture` 의 캐시 키가 파일 stem 이었다. 넷이 함께 틀렸다.
#   ① 다른 폴더의 같은 이름: 뒤에 요청한 파일 대신 먼저 올린 파일이 나왔다(4×4 요청 → 256×256).
#   ② 조회는 늘 `Textures`, 넣기는 용도별 맵: UI·SpriteSheet 는 한 번도 맞지 않아 요청마다 새로 읽었다.
#   ③ SpriteRenderer 가 이름만 저장: 다시 열면 `Textures\<이름>` 을 찾아 유형 폴더 밖 파일이 사라졌다.
#   ④ 삭제 은퇴가 stem 으로 뗌: 다른 폴더의 같은 이름 파일을 지우면 살아 있는 항목이 떨어졌다.
# 이제 키는 확장자까지 포함한 Assets 기준 상대 경로이고(밖이면 절대 경로), 적재·은퇴·번들 보존이
# 같은 함수로 키를 만든다. 이 검사는 넷을 모두 실물로 자극한다 — 창구는 `assets.texture`.
#
# 재지 않은 것: 번들 보존(`RetainAssets` → `UnloadUnusedAssets`)은 CLI 로 번들을 세울 수 없어
# 소스 대조만 한다. 같은 이름의 다른 확장자는 디코더가 확장자를 보는지 확인하지 않아 fixture 가 없다.
#
# 사용법:
#   pwsh Tools/regression/verify-texture-cache-identity.ps1 -Exe Bin/x64-Release/Editor/CreatorEditor.exe
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$Exe = [IO.Path]::GetFullPath($Exe)
$Work = [IO.Path]::GetFullPath($Work)
$repoRoot = Split-Path (Split-Path $PSScriptRoot -Parent) -Parent
$assets = Join-Path $repoRoot 'Dynamic_CPP/Assets'
$fixtures = Join-Path $PSScriptRoot 'fixtures/browser-thumbnails'
if (-not (Test-Path -LiteralPath $Exe)) { throw "Editor executable not found: $Exe" }
if (Get-Process CreatorEditor -ErrorAction SilentlyContinue) {
    throw '이미 떠 있는 에디터를 닫아라 — 이 검사는 남의 에디터를 멈추지 않는다.'
}
Remove-Item -LiteralPath $Work -Recurse -Force -ErrorAction SilentlyContinue
New-Item -ItemType Directory -Force -Path $Work | Out-Null

$script:checks = 0
$failures = [Collections.Generic.List[string]]::new()
function Assert([bool]$Condition, [string]$Message) {
    $script:checks++
    if (-not $Condition) { $failures.Add($Message); Write-Host "  FAIL $Message" -ForegroundColor Red }
}

# ── 소스 축: 세 소비자가 같은 신원 함수를 부른다 ─────────────────────────────
$dataSystem = Get-Content -Raw -LiteralPath (Join-Path $repoRoot 'Engine/RenderEngine/DataSystem.cpp')
function BodyOf([string]$signature) {
    $start = $dataSystem.IndexOf($signature)
    if ($start -lt 0) { return '' }
    $next = $dataSystem.IndexOf("`n}", $start)
    $dataSystem.Substring($start, $next - $start)
}
Assert ((BodyOf 'std::shared_ptr<Texture> DataSystem::LoadSharedTexture(') -match 'TextureCacheKey\(assetPath\)') 'LoadSharedTexture 가 TextureCacheKey 로 키를 만들지 않는다'
Assert ((BodyOf 'void DataSystem::RetireCachedAsset(') -match 'TextureCacheKey\(path\)') 'RetireCachedAsset 이 TextureCacheKey 로 떼지 않는다'
$retain = BodyOf 'void DataSystem::RetainAssets('
Assert (([regex]::Matches($retain, 'TextureCacheKey\(ResolveRuntimeAssetPath\(')).Count -eq 3) 'RetainAssets 가 텍스처 세 용도의 키를 적재와 같은 해석으로 만들지 않는다 — 언로드가 번들 텍스처를 지운다'

function Invoke-Editor([string]$Name, [string[]]$Lines, [scriptblock]$AtMark) {
    $scriptPath = Join-Path $Work "$Name.txt"
    $resultPath = Join-Path $Work "$Name.jsonl"
    Set-Content -LiteralPath $scriptPath -Encoding UTF8 -Value $Lines
    $workspace = Join-Path $Work "$Name-ws"
    New-Item -ItemType Directory -Force -Path $workspace | Out-Null
    $env:CREATOR_EDITOR_WORKSPACE_DIR = $workspace
    $env:CREATOR_EDITOR_LEGACY_INI = Join-Path $workspace 'none.ini'
    $process = Start-Process -FilePath $Exe -WorkingDirectory (Split-Path $Exe) -WindowStyle Hidden -PassThru `
        -ArgumentList @('--script', ('"' + $scriptPath + '"'), '--result-format', 'jsonl', '--result-file', ('"' + $resultPath + '"')) `
        -RedirectStandardOutput "$Work/$Name.out" -RedirectStandardError "$Work/$Name.err"
    try {
        if ($AtMark) {
            $mark = Join-Path $Work "$Name.mark"
            $clock = [Diagnostics.Stopwatch]::StartNew()
            while (-not (Test-Path -LiteralPath $mark)) {
                if ($process.HasExited) { throw "${Name}: 표지 전에 에디터가 끝났다(exit $($process.ExitCode))" }
                if ($clock.Elapsed.TotalSeconds -gt 180) { throw "${Name}: 표지가 180 초 안에 안 생겼다" }
                Start-Sleep -Milliseconds 20
            }
            & $AtMark
        }
        if (-not $process.WaitForExit(300000)) { $process.Kill(); throw "${Name}: 에디터가 제때 끝나지 않았다" }
    }
    finally {
        if (-not $process.HasExited) { $process.Kill() }
        Remove-Item Env:CREATOR_EDITOR_WORKSPACE_DIR -ErrorAction SilentlyContinue
        Remove-Item Env:CREATOR_EDITOR_LEGACY_INI -ErrorAction SilentlyContinue
    }
    $rows = @(Get-Content -LiteralPath $resultPath | Where-Object { $_.Trim() } | ForEach-Object { $_ | ConvertFrom-Json })
    if ($rows.Count -ne $Lines.Count) { throw "${Name}: 결과 행 $($rows.Count) 이 줄 수 $($Lines.Count) 와 다르다(exit $($process.ExitCode))" }
    for ($i = 0; $i -lt $rows.Count; $i++) {
        if ($rows[$i].status -ne 'succeeded') { throw "${Name}: '$($Lines[$i])' 가 $($rows[$i].status): $($rows[$i].message)" }
    }
    [pscustomobject]@{ Rows = $rows; Lines = $Lines }
}
function Row($run, [string]$line) {
    for ($i = 0; $i -lt $run.Lines.Count; $i++) { if ($run.Lines[$i] -eq $line) { return $run.Rows[$i] } }
    throw "줄이 없다: $line"
}
function Entry($data, [string]$cache, [string]$key) {
    @($data.$cache | Where-Object { $_.key -eq $key }) | Select-Object -First 1
}

$tag = [guid]::NewGuid().ToString('N').Substring(0, 8)
$folders = @("TexIdA_$tag", "TexIdB_$tag", "TexIdC_$tag")
$keyA = "TexIdA_$tag/Same_$tag.png"; $keyB = "TexIdB_$tag/Same_$tag.png"; $keyC = "TexIdC_$tag/Ui_$tag.png"
$pathA = Join-Path $assets $keyA; $pathB = Join-Path $assets $keyB; $pathC = Join-Path $assets $keyC
$scene = Join-Path $Work 'TextureIdentity.creator'
$summary = ''
try {
    foreach ($folder in $folders) { New-Item -ItemType Directory -Force -Path (Join-Path $assets $folder) | Out-Null }
    Copy-Item -LiteralPath (Join-Path $fixtures 'Checker256.png') -Destination $pathA
    Copy-Item -LiteralPath (Join-Path $fixtures 'Tiny4.png') -Destination $pathB
    Copy-Item -LiteralPath (Join-Path $fixtures 'Checker256.png') -Destination $pathC

    $lines = @('wait 60',
        "assets.texture load texture $pathA",
        "assets.texture load texture $pathB",
        "assets.texture load texture $keyA",
        "assets.texture load ui $pathC",
        "assets.texture load ui $keyC",
        "assets.texture load ui $pathA",
        'scene.new TextureIdentity', 'wait 30',
        'object.create SpriteHolder', 'component.add SpriteHolder SpriteRenderer',
        "object.property SpriteHolder SpriteRenderer m_SpritePath $pathA",
        "scene.save $scene", 'wait 10',
        # 여는 순간 OnDeserialized → SetSprite 가 m_SpritePath 를 다시 적는다(예전엔 이름).
        "scene.switch $scene", 'wait 60',
        'object.properties SpriteHolder SpriteRenderer',
        "scene.save $scene", 'wait 10',
        "scene.save $(Join-Path $Work 'first.mark')")
    for ($i = 0; $i -lt 200; $i++) { $lines += 'wait 10'; $lines += 'assets.texture ' }
    $lines += 'quit'
    $first = Invoke-Editor 'first' $lines { Start-Sleep -Milliseconds 200; Remove-Item -LiteralPath $pathB }

    # ① 다른 폴더의 같은 이름
    $a = (Row $first "assets.texture load texture $pathA").data.loaded
    $b = (Row $first "assets.texture load texture $pathB").data
    Assert ($a.width -eq 256) "A 가 256×256 이 아니다($($a.width))"
    Assert ($b.loaded.width -eq 4) "①: 다른 폴더의 같은 이름 B(4×4)를 요청했는데 $($b.loaded.width)×$($b.loaded.height) 가 나왔다 — stem 키가 A 를 돌려줬다"
    Assert ($b.loaded.instance -ne $a.instance) '①: A 와 B 가 같은 인스턴스다'
    Assert ($null -ne (Entry $b 'textures' $keyA) -and $null -ne (Entry $b 'textures' $keyB)) "①: 캐시에 두 키($keyA, $keyB)가 따로 없다"
    $again = (Row $first "assets.texture load texture $keyA").data.loaded
    Assert ($again.instance -eq $a.instance) '저장 형식(상대 경로)으로 다시 요청했는데 A 와 다른 인스턴스다 — 신원이 해석과 어긋난다'

    # ② 용도별 맵의 조회·넣기
    $ui1 = (Row $first "assets.texture load ui $pathC").data
    $ui2 = (Row $first "assets.texture load ui $keyC").data
    Assert ($ui1.loaded.instance -eq $ui2.loaded.instance) "②: 같은 UI 텍스처를 두 번 요청했는데 인스턴스가 둘이다($($ui1.loaded.instance), $($ui2.loaded.instance)) — 조회와 넣기가 다른 맵을 본다"
    Assert (@($ui2.uiTextures | Where-Object { $_.key -eq $keyC }).Count -eq 1) '②: UI 맵에 C 의 키가 하나가 아니다'
    $uiA = (Row $first "assets.texture load ui $pathA").data
    Assert ($uiA.loaded.instance -ne $a.instance) '②: 같은 파일의 UI 요청이 일반 텍스처 맵의 인스턴스를 받았다 — 용도가 섞였다'
    Assert ($null -ne (Entry $uiA 'uiTextures' $keyA)) '②: UI 로 요청한 A 가 UI 맵에 없다'

    # ③ 저장 신원
    $reopened = [string](Row $first 'object.properties SpriteHolder SpriteRenderer').data.values.m_SpritePath
    Assert ($reopened -eq $keyA) "③: 다시 연 SpriteRenderer 가 '$reopened' 을 적었다($keyA 여야 한다) — 이름만 적으면 Textures 폴더에서 찾는다"

    # ④ 삭제 은퇴
    $polls = @(for ($i = 0; $i -lt $first.Lines.Count; $i++) { if ($first.Lines[$i] -eq 'assets.texture ') { $first.Rows[$i].data } })
    Assert ($null -ne (Entry $polls[0] 'textures' $keyB)) '④: 지우기 전 표집에 B 가 없다 — 자극 전 상태를 못 봤다'
    $gone = @($polls | Where-Object { $null -eq (Entry $_ 'textures' $keyB) })
    Assert ($gone.Count -gt 0) '④: B 를 지웠는데 200 번 표집 동안 B 항목이 안 떨어졌다 — 은퇴가 자극되지 않았다'
    $survivor = Entry $polls[-1] 'textures' $keyA
    Assert ($null -ne $survivor -and $survivor.instance -eq $a.instance) "④: B 를 지웠더니 A 항목이 떨어지거나 바뀌었다 — 은퇴가 stem 으로 뗐다"

    # ③ 다음 기동에서 그 신원으로 다시 올라오는가
    $second = Invoke-Editor 'second' @('wait 60', "scene.switch $scene", 'wait 120', 'object.properties SpriteHolder SpriteRenderer', 'assets.texture', 'quit') $null
    $cold = (Row $second 'assets.texture').data
    $coldA = Entry $cold 'textures' $keyA
    Assert ($null -ne $coldA -and $coldA.width -eq 256) "③: 새 기동에서 씬을 열었는데 $keyA 가 올라오지 않았다 — 저장된 신원이 파일로 돌아오지 않는다"
    Assert ([string](Row $second 'object.properties SpriteHolder SpriteRenderer').data.values.m_SpritePath -eq $keyA) '③: 새 기동의 m_SpritePath 가 신원과 다르다'

    $summary = "A $($a.width) · B $($b.loaded.width) · UI 재요청 같은 인스턴스 · 저장 '$reopened' · B 삭제 뒤 A 생존 · 새 기동 $($coldA.width)"
}
finally {
    foreach ($folder in $folders) {
        $full = Join-Path $assets $folder
        Remove-Item -LiteralPath $full -Recurse -Force -ErrorAction SilentlyContinue
        Remove-Item -LiteralPath "$full.meta" -Force -ErrorAction SilentlyContinue
    }
}

Write-Host ''
Write-Host "  $summary"
if ($failures.Count -gt 0) { throw "텍스처 캐시 신원 검사 실패 $($failures.Count) 건" }
Write-Host "텍스처 캐시 신원 검사: 단정 $script:checks · PASS"
