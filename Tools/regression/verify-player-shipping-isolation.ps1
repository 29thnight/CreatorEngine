[CmdletBinding()]
param(
    [ValidateSet('Debug', 'Release')]
    [string]$Config = 'Debug',

    # 두 구성을 여기서 짓는다. 이미 지어 두었으면 건너뛴다.
    [switch]$SkipBuild
)

# PHASE 14.5 LC8 (§11.2 · §12.1) — Shipping Player 에 서비스·소켓이 0 인가.
#
# ── 무엇을 단정하는가 ───────────────────────────────────────────────────
#
#   ① Development Player 는 소켓을 import 하고 서비스 문자열을 갖는다.
#   ② Shipping Player 는 **둘 다 0** 이다.
#   ③ 두 바이너리가 나란히 존재한다(같은 자리를 덮어쓰지 않는다).
#
# ── ①이 왜 단정인가 ─────────────────────────────────────────────────────
#
# ②만 보면 이 게이트는 **아무것도 안 하고도 통과한다.** 링크가 통째로 깨졌거나,
# 잘못된 경로를 봤거나, Player 에 애초에 서비스가 없어도 "Shipping 에 소켓 0" 은
# 참이다. 있어야 할 곳에 있는 것을 먼저 확인해야 없는 것이 뜻을 갖는다.
#
# ── 왜 문자열이 아니라 import 표인가 ────────────────────────────────────
#
# 문자열 스캔은 컴파일러·링커가 리터럴을 접거나 남기는 사정에 흔들린다. import
# 디렉터리는 **링크 결과의 사실**이다 — `WS2_32.dll` 이 거기 있으면 이 바이너리는
# 소켓 API 를 부를 수 있고, 없으면 부를 수 없다. 문자열은 보조 증거로만 본다.

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$repoRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
$devExe   = Join-Path $repoRoot "Bin\x64-$Config\Player\Player.exe"
$shipExe  = Join-Path $repoRoot "Bin\x64-$Config-Shipping\Player\Player.exe"

function Find-Tool([string]$Name) {
    $vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
    if (Test-Path -LiteralPath $vswhere) {
        $root = & $vswhere -latest -products '*' -property installationPath
        if ($root) {
            $hit = Get-ChildItem -Path (Join-Path $root "VC\Tools\MSVC\*\bin\Hostx64\x64\$Name") -ErrorAction SilentlyContinue |
                   Select-Object -First 1
            if ($hit) { return $hit.FullName }
        }
    }
    $cmd = Get-Command $Name -ErrorAction SilentlyContinue
    if ($cmd) { return $cmd.Source }
    return $null
}

if (-not $SkipBuild) {
    $vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
    $msbuild = $null
    if (Test-Path -LiteralPath $vswhere) {
        $msbuild = & $vswhere -latest -products '*' -requires Microsoft.Component.MSBuild `
                   -find 'MSBuild\**\Bin\amd64\MSBuild.exe' | Select-Object -First 1
    }
    if (-not $msbuild) { 'MSBuild 를 찾지 못했다'; exit 1 }

    $project = Join-Path $repoRoot 'Player\Player.vcxproj'
    foreach ($shipping in @('false', 'true')) {
        & $msbuild $project '/m' "/p:Configuration=$Config" '/p:Platform=x64' `
            "/p:EngineShipping=$shipping" '/nologo' '/verbosity:minimal' | Out-Null
        if ($LASTEXITCODE -ne 0) { "Player 빌드 실패 (EngineShipping=$shipping)"; exit 1 }
    }
}

$failures = New-Object System.Collections.Generic.List[string]

# ── 3) 두 바이너리가 나란히 있는가 ──────────────────────────────────────
#
# 산출물 키가 갈리지 않으면 뒤에 지은 쪽이 앞선 쪽을 덮고, 아래 대조는 같은
# 파일을 두 번 보게 된다 — 그러면 ①과 ②가 동시에 참일 수 없어 붉어지지만,
# 사유가 "격리 실패" 로 보여 원인을 엉뚱한 곳에서 찾게 된다. 먼저 못 박는다.
foreach ($pair in @(@{ N = 'development'; P = $devExe }, @{ N = 'shipping'; P = $shipExe })) {
    if (-not (Test-Path -LiteralPath $pair.P -PathType Leaf)) {
        "$($pair.N) 바이너리가 없다: $($pair.P)"; exit 1
    }
}
if ((Get-FileHash -LiteralPath $devExe).Hash -eq (Get-FileHash -LiteralPath $shipExe).Hash) {
    $failures.Add('side-by-side : 두 구성의 바이너리가 완전히 같다 — 산출물 키가 갈리지 않아 한쪽이 다른 쪽을 덮었다')
}

$dumpbin = Find-Tool 'dumpbin.exe'
if (-not $dumpbin) { 'dumpbin.exe 를 찾지 못했다 — 이 게이트는 import 표를 읽어야 한다'; exit 1 }

function Get-Imports([string]$Exe) {
    $out = & $dumpbin /DEPENDENTS $Exe 2>$null
    return @($out | ForEach-Object { $_.Trim() } | Where-Object { $_ -match '\.dll$' })
}
function Test-Marker([string]$Exe, [string]$Marker) {
    $text = [Text.Encoding]::ASCII.GetString([IO.File]::ReadAllBytes($Exe))
    return $text.Contains($Marker)
}

# 링크 결과의 사실. 소켓 API 를 부를 수 있는가.
$devImports  = Get-Imports $devExe
$shipImports = Get-Imports $shipExe
$devSocket   = @($devImports  | Where-Object { $_ -match '(?i)ws2_32\.dll' }).Count -gt 0
$shipSocket  = @($shipImports | Where-Object { $_ -match '(?i)ws2_32\.dll' }).Count -gt 0

"{0,-26} ws2_32={1} imports={2}" -f 'development-links-socket', $devSocket, $devImports.Count
"{0,-26} ws2_32={1} imports={2}" -f 'shipping-drops-socket',   $shipSocket, $shipImports.Count

# ① 있어야 할 곳에 있다 — 이것이 없으면 ②는 공허하다.
if (-not $devSocket) {
    $failures.Add('development-links-socket : Development Player 가 소켓을 import 하지 않는다 — 서비스가 링크되지 않았고, 그러면 아래 "Shipping 에 0" 은 아무것도 확인하지 않는다')
}
# ② 없어야 할 곳에 없다.
if ($shipSocket) {
    $failures.Add('shipping-drops-socket : Shipping Player 가 소켓을 import 한다 — 구성 조건부 ProjectReference 가 듣지 않았다(§12.1)')
}

# 보조 증거. 문자열은 링커 사정에 흔들리므로 import 표 다음에 본다.
$markers = @('WSAStartup', 'endpoint.json', 'CommandService')
foreach ($marker in $markers) {
    $inDev  = Test-Marker $devExe  $marker
    $inShip = Test-Marker $shipExe $marker
    "{0,-26} dev={1} ship={2}" -f "marker:$marker", $inDev, $inShip
    if ($inShip) {
        $failures.Add("marker:$marker : Shipping 바이너리에 서비스 문자열이 남아 있다")
    }
}

# ── Player registry 가 role 로 갈리는가 (§11.2) ─────────────────────────
#
# 실행 중인 Player 에 붙어 `/commands` 를 읽는 것이 정본 검사이지만, Player 는
# 패키지된 호스트라 스테이징 없이는 뜨지 않는다(런타임 콘텐츠 준비 실패로 exit 2).
# 그 검사는 `Tools/build.ps1` 의 스모크가 맡는다. 여기서는 **표 자체**를 본다 —
# seed 표에서 Player role 을 가진 이름과 Player 가 등록하는 이름이 일치하는지.
$seedFile = Join-Path $repoRoot 'Editor\EngineEntry\CommandCore\CommandDescriptorSeeds.cpp'
$playerFile = Join-Path $repoRoot 'Player\PlayerCommands.cpp'
$seedText = [IO.File]::ReadAllText($seedFile)
$playerText = [IO.File]::ReadAllText($playerFile)

$seedPlayerNames = @([regex]::Matches($seedText,
    '\{\s*"([^"]+)"[^\r\n]*CommandRoles::(Player|Both)') |
    ForEach-Object { $_.Groups[1].Value } | Sort-Object -Unique)
$registeredNames = @([regex]::Matches($playerText,
    '\{\s*"([^"]+)",\s*&Cmd_') |
    ForEach-Object { $_.Groups[1].Value } | Sort-Object -Unique)

"{0,-26} seed={1} 등록={2}" -f 'player-roles', $seedPlayerNames.Count, $registeredNames.Count

$registeredWithoutRole = @($registeredNames | Where-Object { $seedPlayerNames -notcontains $_ })
if ($registeredWithoutRole.Count -gt 0) {
    $failures.Add("player-roles : role 에 Player 가 없는데 등록 표에 있다: $($registeredWithoutRole -join ', ') — 런타임이 거부하겠지만 표가 거짓말을 하고 있다")
}
$roleWithoutRegistration = @($seedPlayerNames | Where-Object { $registeredNames -notcontains $_ })
if ($roleWithoutRegistration.Count -gt 0) {
    $failures.Add("player-roles : role 은 Player 인데 등록되지 않았다: $($roleWithoutRegistration -join ', ') — 그 명령은 조용히 사라진다")
}

""
if ($failures.Count -gt 0) {
    "실패:"
    $failures | ForEach-Object { "  - $_" }
    exit 1
}
"Player Shipping 격리 통과 — Development 는 소켓을 링크하고 Shipping 은 링크하지 않는다"
exit 0
