# 블렌더로 기본 도형을 뽑아 엔진 에셋 폴더에 넣는다.
#
#   pwsh Tools/blender/export-primitives.ps1
#
# 블렌더 경로를 자동으로 찾는다. 여러 버전이 깔려 있으면 가장 최신을 쓰고,
# -Blender로 직접 지정할 수도 있다.
param(
    [string]$Blender = "",
    [string]$OutDir = ""
)

$ErrorActionPreference = "Stop"
$repoRoot = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)

if ([string]::IsNullOrWhiteSpace($OutDir)) {
    $OutDir = Join-Path $repoRoot "Dynamic_CPP\Assets\Models"
}

function Find-Blender {
    $candidates = @()

    foreach ($pattern in @(
        "$env:ProgramFiles\Blender Foundation\*\blender.exe",
        "${env:ProgramFiles(x86)}\Blender Foundation\*\blender.exe",
        "$env:LOCALAPPDATA\Programs\Blender*\blender.exe"
    )) {
        $candidates += Get-ChildItem $pattern -ErrorAction SilentlyContinue
    }

    $onPath = Get-Command blender -ErrorAction SilentlyContinue
    if ($onPath) { $candidates += Get-Item $onPath.Source }

    if ($candidates.Count -eq 0) { return $null }

    # 디렉터리 이름에 버전이 들어가므로 이름 역순이면 최신이 앞에 온다.
    return ($candidates | Sort-Object FullName -Descending | Select-Object -First 1).FullName
}

if ([string]::IsNullOrWhiteSpace($Blender)) { $Blender = Find-Blender }

if ([string]::IsNullOrWhiteSpace($Blender) -or -not (Test-Path $Blender)) {
    Write-Host "블렌더를 찾지 못했다. -Blender <경로>로 지정할 것." -ForegroundColor Red
    exit 1
}

$script = Join-Path $PSScriptRoot "export_primitives.py"
Write-Host "블렌더: $Blender"
Write-Host "스크립트: $script"

# --factory-startup으로 사용자 설정(애드온·기본 씬)을 배제한다. 안 그러면
# 다른 기계에서 다른 결과가 나오고, 그건 검증 자산으로 못 쓴다.
$output = & $Blender --background --factory-startup --python $script -- --out $OutDir 2>&1
$exit = $LASTEXITCODE

$output | Where-Object { $_ -match "^\[도형\]" -or $_ -match "Error|Traceback" } | ForEach-Object { Write-Host $_ }

if ($exit -ne 0) {
    Write-Host "블렌더 실행 실패 (종료 코드 $exit)" -ForegroundColor Red
    $output | Select-Object -Last 30 | ForEach-Object { Write-Host $_ }
    exit 1
}

$produced = Get-ChildItem (Join-Path $OutDir "Prim_*.glb") -ErrorAction SilentlyContinue
if (-not $produced) {
    Write-Host "glb가 하나도 생기지 않았다: $OutDir" -ForegroundColor Red
    exit 1
}

Write-Host ""
Write-Host "출력: $OutDir ($($produced.Count)개)" -ForegroundColor Green
exit 0
