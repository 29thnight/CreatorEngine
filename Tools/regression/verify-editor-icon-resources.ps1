[CmdletBinding()]
param(
    [string]$Root = (Join-Path $PSScriptRoot '../..')
)
# 에디터 아이콘 자원의 저작 원본을 못 박는다.
#
# 착수 실측(2026-09-11): 원본이 `x64\Icons` 에 있었다. 그 이름은 Visual Studio
# 출력 관례라 `.gitignore` 의 `x64/` 가 통째로 덮었고, 그 안의 13개는 과거에
# 강제 추가된 덕에 **추적 중이면서 동시에 무시되는** 상태로 살아 있었다.
# 이미 추적된 파일은 ignore 영향을 받지 않으므로 빌드도 게이트도 전부 초록인데,
# 아이콘을 하나 더 넣는 순간 `git add` 가 조용히 거부한다. 즉 결함은 "지금"이
# 아니라 "다음 추가"에 나타나고, 런타임으로는 영원히 관측되지 않는다.
# 그래서 정적으로 본다.
#
# 같은 자리에 구 DX 셰이더(.cso) 복사가 있었고 그쪽은 원본이 구성별로 갈렸다 —
# Debug 는 존재하지 않는 `x64\Assets\Shaders`, Release 는 추적 밖의
# `Bin\Assets\Shaders`. `PrecompiledShaderPath` 를 읽는 코드는 한 줄도 없어
# 소비자 0인 복사였으므로 걷었다. 되돌아오는 길도 여기서 막는다.
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$Root = [IO.Path]::GetFullPath($Root)
$iconRoot = Join-Path $Root 'Resources/Editor/Icons'
$targets = Join-Path $Root 'Directory.Build.targets'

$script:checks = 0
function Assert([bool]$Condition, [string]$Message) {
    if (-not $Condition) { throw $Message }
    $script:checks++
}

function Invoke-Git([string[]]$GitArgs) {
    $output = & git -C $Root @GitArgs 2>&1
    return @{ ExitCode = $LASTEXITCODE; Output = @($output) }
}

# ① 저작 원본이 있고 비어 있지 않다. 빌드 규칙이 여기서 읽으므로 비면 빌드가
#    아이콘 없는 에디터를 조용히 만들어 낸다(옮기기 전 Debug 셰이더가 그랬다).
Assert (Test-Path -LiteralPath $iconRoot -PathType Container) `
    "icon source root is missing: $iconRoot"

$diskFiles = @(Get-ChildItem -LiteralPath $iconRoot -Recurse -File -Force |
    ForEach-Object { $_.FullName.Substring($iconRoot.Length + 1).Replace('\', '/') } |
    Sort-Object)
Assert ($diskFiles.Count -gt 0) "icon source root is empty: $iconRoot"

# ② 디스크에 있는 원본 파일이 **전부** 추적된다. 이것이 이 게이트의 중심이다.
#    한 장이라도 빠지면 그 아이콘은 내 기계에서만 보이고 남의 체크아웃에서는
#    빌드가 복사할 것이 없다.
$tracked = Invoke-Git @('ls-files', '--', 'Resources/Editor/Icons')
Assert ($tracked.ExitCode -eq 0) "git ls-files failed: $($tracked.Output -join "`n")"
$trackedFiles = @($tracked.Output |
    Where-Object { $_ -like 'Resources/Editor/Icons/*' } |
    ForEach-Object { $_.Substring('Resources/Editor/Icons/'.Length) } |
    Sort-Object)

$untracked = @($diskFiles | Where-Object { $trackedFiles -notcontains $_ })
Assert ($untracked.Count -eq 0) `
    ("icon source files are not tracked by git: " + ($untracked -join ', '))

# ③ 원본 자리 자체가 ignore 규칙에 걸리지 않는다. ②는 이미 추적된 파일만 보므로
#    "추적 중이면서 동시에 무시되는" 옛 상태를 통과시킨다. 아직 없는 파일 이름을
#    물어야 규칙을 본다.
$probe = Invoke-Git @('check-ignore', '-q', '--no-index', 'Resources/Editor/Icons/__gate_probe__.png')
Assert ($probe.ExitCode -ne 0) `
    'Resources/Editor/Icons is covered by a .gitignore rule; new icons would be silently dropped'

# ④ 빌드 규칙이 그 원본을 읽고, 구성별로 갈리지 않는다. 구성 분기가 들어오면
#    Debug 와 Release 의 배포본이 서로 다른 그림을 갖게 된다.
Assert (Test-Path -LiteralPath $targets -PathType Leaf) "missing: $targets"
$targetsText = Get-Content -LiteralPath $targets -Raw
Assert ($targetsText -match [regex]::Escape('$(SolutionDir)Resources\Editor\Icons\')) `
    'Directory.Build.targets no longer reads Resources\Editor\Icons'

$deployBlock = [regex]::Match(
    $targetsText,
    '<Target\s+Name="DeployEditorResources".*?</Target>',
    [Text.RegularExpressions.RegexOptions]::Singleline)
Assert ($deployBlock.Success) 'DeployEditorResources target is missing'
Assert (-not ($deployBlock.Value -match '\$\(Configuration\)')) `
    'DeployEditorResources branches on $(Configuration); editor resources must not differ by build configuration'

# ⑤ 옛 자리로 되돌아가는 길을 막는다. 코드와 빌드 파일만 본다 — 문서가 옛 자리를
#    이력으로 적는 것은 결함이 아니다. 이 게이트 파일 자신은 그 이름을 설명으로
#    적으므로 뺀다.
$codeSpec = @('*.h', '*.cpp', '*.cs', '*.targets', '*.props', '*.vcxproj', '*.ps1')
$legacy = Invoke-Git (@('grep', '-l', '-I', '-F', '-e', 'x64\Icons', '-e', 'x64/Icons', '--') + $codeSpec)
if ($legacy.ExitCode -eq 0) {
    $offenders = @($legacy.Output | Where-Object { $_ -notlike '*verify-editor-icon-resources.ps1' })
    Assert ($offenders.Count -eq 0) `
        ("legacy icon source path x64\Icons is referenced again: " + ($offenders -join ', '))
} else {
    $script:checks++
}

# ⑥ 소비자 0이던 구 셰이더 경로가 되살아나지 않는다. 소스만 본다 — 계획서와
#    대시보드가 이력으로 적는 이름까지 막으면 기록을 지우게 된다.
$deadPath = Invoke-Git @('grep', '-l', '-I', '-F',
    '-e', 'PrecompiledShaderPath', '-e', 'RelativeToPrecompiledShader',
    '--', '*.h', '*.cpp', '*.cs')
if ($deadPath.ExitCode -eq 0) {
    $offenders = @($deadPath.Output | Where-Object { $_ -notlike '*verify-editor-icon-resources.ps1' })
    Assert ($offenders.Count -eq 0) `
        ("PrecompiledShaderPath is back without a consumer: " + ($offenders -join ', '))
} else {
    $script:checks++
}

# ⑦ 코드가 이름으로 부르는 아이콘이 전부 원본에 있다. 이름은 문자열이라
#    컴파일러가 안 봐 준다 — 파일을 지우거나 오타를 내면 런타임에 빈 텍스처가
#    되고, 그 자리는 "아이콘이 없는 것"과 구분되지 않는다.
$consumerFiles = @(
    'Editor/EngineEntry/EditorAssetPresentation.cpp',
    'Editor/EngineEntry/App.cpp',
    'Editor/RenderTests/RHI/DX12/Tests/Editor/EnhancedGizmoIconTest.cpp',
    'Editor/RenderTests/RHI/DX12/Tests/Editor/EnhancedGizmoSceneTest.cpp'
)
$referenced = [Collections.Generic.SortedSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
foreach ($relative in $consumerFiles) {
    $path = Join-Path $Root $relative
    Assert (Test-Path -LiteralPath $path -PathType Leaf) "icon consumer moved or was removed: $relative"
    $text = Get-Content -LiteralPath $path -Raw
    foreach ($match in [regex]::Matches($text, 'L"([^"\\/]+\.(?:png|bmp))"',
        [Text.RegularExpressions.RegexOptions]::IgnoreCase)) {
        [void]$referenced.Add($match.Groups[1].Value)
    }
}
Assert ($referenced.Count -gt 0) 'no icon filename literal was found; the extraction pattern went stale'

$missing = @($referenced | Where-Object { $diskFiles -notcontains $_ })
Assert ($missing.Count -eq 0) `
    ("icons referenced by code are missing from the source root: " + ($missing -join ', '))

# 쓰이지 않는 원본은 실패가 아니라 보고다. 아직 배선되지 않은 그림이 있을 수
# 있고(지금은 Folder.png 하나), 그것을 여기서 막으면 그림을 먼저 넣는 작업이
# 붉어진다.
$unused = @($diskFiles | Where-Object { -not $referenced.Contains($_) })

# ⑧ 씬 기즈모 아이콘은 128x128 이다. 크기가 계약인 이유는 소비 경로다 —
#    `Texture::LoadSharedFromPath` 가 WIC 로 **밉 없이** 한 벌만 올리고, 기즈모
#    아이콘 패스는 월드 1 단위 빌보드에 선형 샘플러로 찍는다. 원본이 크면 화면
#    수십 px 로 밉 없이 줄어 가는 선이 깨지고, 메모리는 장당 RGBA 로 CPU 사본과
#    GPU 에 한 벌씩 든다(1254 px 면 장당 6.3 MB, 128 px 는 65 KB).
#    2026-09-16 에 다섯 장이 새 그림으로 바뀌면서 1254x1254 원본이 그대로 실렸고
#    dx12.gizmoicon 이 카메라 한 장만 크기를 보고 있어 나머지 넷은 아무도 못 봤다.
#    이름은 제품 소비자(`gizmoIcons->x = loadIcon(L"...")`)에서 뽑는다.
$presentation = Get-Content -LiteralPath (Join-Path $Root 'Editor/EngineEntry/EditorAssetPresentation.cpp') -Raw
$gizmoIcons = @([regex]::Matches($presentation, 'gizmoIcons->\w+\s*=\s*loadIcon\(L"([^"]+)"\)') |
    ForEach-Object { $_.Groups[1].Value })
Assert ($gizmoIcons.Count -ge 5) `
    "gizmo icon consumers were not found ($($gizmoIcons.Count)); the extraction pattern went stale"
$kGizmoIconPixels = 128
foreach ($name in $gizmoIcons) {
    $bytes = [IO.File]::ReadAllBytes((Join-Path $iconRoot $name))
    $isPng = $bytes.Length -ge 26 -and $bytes[1] -eq 0x50 -and $bytes[2] -eq 0x4E -and $bytes[3] -eq 0x47
    Assert $isPng "gizmo icon is not a PNG: $name"
    $width = ([int]$bytes[16] -shl 24) -bor ([int]$bytes[17] -shl 16) -bor ([int]$bytes[18] -shl 8) -bor [int]$bytes[19]
    $height = ([int]$bytes[20] -shl 24) -bor ([int]$bytes[21] -shl 16) -bor ([int]$bytes[22] -shl 8) -bor [int]$bytes[23]
    # 색 형식 6 = RGBA. 알파가 없으면 빌보드가 사각형으로 찍힌다.
    Assert ($width -eq $kGizmoIconPixels -and $height -eq $kGizmoIconPixels -and $bytes[25] -eq 6) `
        "gizmo icon must be ${kGizmoIconPixels}x${kGizmoIconPixels} RGBA (no mips on the live path): $name is ${width}x${height} colorType $($bytes[25])"
}

Write-Host ("icon source root   : " + $iconRoot)
Write-Host ("tracked icon files : " + $diskFiles.Count)
Write-Host ("referenced by code : " + $referenced.Count)
if ($unused.Count -gt 0) {
    Write-Host ("unreferenced       : " + ($unused -join ', '))
}
Write-Host ("[OK] editor icon resources: $($script:checks) checks passed")

# ★ 종료 코드를 명시한다(2026-09-20). 이 스크립트의 마지막 네이티브 명령은 ⑥의
#   `git grep` 인데, **매치가 없으면 git grep 은 1을 낸다** — 즉 "되살아난 참조가
#   하나도 없다"는 성공 조건이 그대로 $LASTEXITCODE 1 로 남는다. exit 를 적지 않으면
#   pwsh -File 이 그 값을 프로세스 종료 코드로 쓰므로, 모든 단정을 통과한 실행이
#   "[OK] ... checks passed" 를 찍으면서 붉게 보인다. 판정이 종료 코드뿐인
#   집중 검사 방식에서는 통과와 실패가 **둘 다 1** 이라 게이트가 판정 능력을 잃는다.
#   실패는 Assert 의 throw 가 $ErrorActionPreference='Stop' 아래서 비0 으로 끝내므로,
#   여기서는 성공만 못 박으면 된다.
exit 0
