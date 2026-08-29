#Requires -Version 7
<#
.SYNOPSIS
    씬/프리팹 인라인 재질의 legacy texture 이름 참조를 GUID 로 이주한다.

.DESCRIPTION
    저작 데이터 이주다. **새 변환기를 만들지 않는다** — 정본 경로를 태운다.

    `DataSystem::SynchronizeLegacyMaterialProperties` 가 이미 두 방향을 채운다:
    이름에서 GUID 를 해석해 `m_propertyValues` 항목을 만들고, GUID 에서 이름도
    되채운다. 즉 **씬을 로드했다가 저장하기만 하면** 인라인 재질이 GUID 를
    갖는다. 이 스크립트는 그것을 에디터 CLI(`scene.switch` → `scene.save`)로
    돌리고 결과를 검증할 뿐이다.

    이름 필드는 남는다(위 함수가 되채운다). 그래도 무방하다 — 런타임은 GUID
    우선이고, 쿠커의 `legacyTextureNameRefs` 도 "대응 GUID 가 없는" 경우만
    센다. 이름 폴백 자체를 없애는 것은 I5/I6 의 일이다.

    기본은 dry-run 이다. -Apply 를 줘야 씬을 다시 쓴다.
#>
param(
    [string]$Root = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path,
    [string]$Exe,
    [switch]$Apply,
    [int]$TimeoutSeconds = 300
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

if (-not $Exe) {
    $Exe = Join-Path $Root 'Bin\x64-Debug\Editor\CreatorEditor.exe'
}
if (-not (Test-Path -LiteralPath $Exe -PathType Leaf)) {
    "에디터 실행 파일이 없다: $Exe"
    exit 2
}

$scenesRoot = Join-Path $Root 'Dynamic_CPP\Assets\Scenes'
$legacyKeys = @(
    @{ Field = 'm_baseColorTexName'; Property = 'baseColorMap' }
    @{ Field = 'm_normalTexName';    Property = 'normalMap' }
    @{ Field = 'm_ORM_TexName';      Property = 'ormMap' }
    @{ Field = 'm_AO_TexName';       Property = 'aoMap' }
    @{ Field = 'm_EmissiveTexName';  Property = 'emissiveMap' }
)

function Measure-LegacyNameReferences {
    param([Parameter(Mandatory)][string]$Path)

    # 쿠커와 같은 기준: 이름이 비어 있지 않고 **대응 property GUID 가 없는** 것만 센다.
    # 여기서는 파일 단위 근사로 충분하다 — 정확한 판정은 쿠커가 한다.
    $text = [IO.File]::ReadAllText($Path)
    $named = 0
    foreach ($key in $legacyKeys) {
        $named += @([regex]::Matches($text,
            ('(?m)^\s+{0}: "?(?!"")[^"\r\n]+"?\s*$' -f $key.Field))).Count
    }
    $guids = @([regex]::Matches($text,
        '(?m)^\s+m_textureGuid: (?!00000000-0000-0000-0000-000000000000)[0-9a-f-]{36}\s*$')).Count
    return [pscustomobject]@{ NamedFields = $named; TextureGuids = $guids }
}

$scenes = @(Get-ChildItem -LiteralPath $scenesRoot -File -Filter '*.creator' | Sort-Object Name)
if ($scenes.Count -eq 0) { "씬이 없다: $scenesRoot"; exit 2 }

$targets = @($scenes | Where-Object {
    (Measure-LegacyNameReferences -Path $_.FullName).NamedFields -gt 0
})

$mode = if ($Apply) { 'APPLY' } else { 'DRY-RUN' }
"scene-texture-guid-migration mode=$mode scenes=$($scenes.Count) targets=$($targets.Count)"
foreach ($scene in $targets) {
    $before = Measure-LegacyNameReferences -Path $scene.FullName
    "  target $($scene.Name) namedFields=$($before.NamedFields) textureGuids=$($before.TextureGuids)"
}
if (-not $Apply -or $targets.Count -eq 0) { exit 0 }

$work = Join-Path $env:TEMP ("CE_SceneTexGuid_" + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $work -Force | Out-Null
$commands = [Collections.Generic.List[string]]::new()
foreach ($scene in $targets) {
    # ★ switch 로 활성 씬을 교체한 뒤 같은 경로에 저장한다. load 만 하면
    #   활성 씬이 바뀌지 않아 save 가 엉뚱한 씬을 쓴다.
    $commands.Add("scene.switch $($scene.FullName)")
    $commands.Add("scene.save $($scene.FullName)")
}
$commands.Add('quit')
$scenario = Join-Path $work 'commands.txt'
$commands | Set-Content -LiteralPath $scenario -Encoding UTF8

$stdout = Join-Path $work 'stdout.txt'
$stderr = Join-Path $work 'stderr.txt'
$process = Start-Process -FilePath $Exe -ArgumentList @('--script', $scenario) `
    -WorkingDirectory $Root -WindowStyle Hidden `
    -RedirectStandardOutput $stdout -RedirectStandardError $stderr -PassThru
$process.WaitForExit($TimeoutSeconds * 1000) | Out-Null
if (-not $process.HasExited) {
    $process.Kill()
    "TIMEOUT output=$work"
    exit 1
}

$failed = 0
foreach ($scene in $targets) {
    $after = Measure-LegacyNameReferences -Path $scene.FullName
    $status = if ($after.TextureGuids -gt 0) { 'ok' } else { 'GUID 미생성' }
    if ($after.TextureGuids -le 0) { ++$failed }
    "  result $($scene.Name) namedFields=$($after.NamedFields) textureGuids=$($after.TextureGuids) $status"
}
"scene-texture-guid-migration exit=$($process.ExitCode) failed=$failed output=$work"
if ($failed -gt 0) { exit 1 }
exit 0
