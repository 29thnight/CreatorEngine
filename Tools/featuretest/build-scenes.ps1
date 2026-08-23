# 기능별 테스트 씬을 저작해 .creator로 저장한다.
#
#   pwsh Tools/featuretest/build-scenes.ps1
#
# 왜 스크립트로 만드는가: 에디터로 손수 만든 씬은 무엇이 왜 그 자리에 있는지가
# 파일 안에 남지 않는다. 여기서는 배치 의도가 코드로 남아, 나중에 "이 씬은 무엇을
# 확인하려고 만든 것인가"를 되짚을 수 있다. 씬이 깨지면 다시 만들면 된다.
#
# 씬마다 엔진을 한 번씩 새로 띄운다. 한 프로세스에서 여러 씬을 연달아 만들면
# 앞 씬의 잔재가 다음 씬에 남을 수 있고, 그러면 무엇을 보고 있는지 불분명해진다.
param(
    [string]$Exe = "",
    [string]$SceneDir = "",
    [int]$TimeoutSec = 180,
    [string[]]$Only = @()
)

$ErrorActionPreference = "Stop"
$repoRoot = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)

if ([string]::IsNullOrWhiteSpace($Exe)) {
    $Exe = Join-Path $repoRoot "x64\Debug\CreatorEditor.exe"
}
if ([string]::IsNullOrWhiteSpace($SceneDir)) {
    $SceneDir = Join-Path $repoRoot "Dynamic_CPP\Assets\Scenes"
}

if (-not (Test-Path $Exe)) {
    Write-Host "실행 파일이 없다: $Exe" -ForegroundColor Red
    exit 1
}

$models = Join-Path $repoRoot "Dynamic_CPP\Assets\Models"
$workDir = Join-Path ([System.IO.Path]::GetTempPath()) "CreatorEngine_FeatureTest"
New-Item -ItemType Directory -Force -Path $workDir | Out-Null

# ── 공통 조각 ──
#
# 모델 임포트는 느리다(assimp 파싱 + GPU 업로드). 넉넉히 기다린다 — 여기서
# 아끼면 배치 명령이 모델을 못 찾고 조용히 넘어간다.
function Import-Models {
    param([string[]]$Names)
    $lines = @()
    foreach ($n in $Names) { $lines += "model.load $models/Prim_$n.glb" }
    $lines += "wait 90"
    return $lines
}

# 카메라와 방향광은 모든 씬에 필요하다. 카메라가 없으면 게임 뷰가 비고,
# 광원이 없으면 전부 검게 나와 무엇이 잘못됐는지 구분되지 않는다.
function New-CameraAndSun {
    param(
        [string]$CamPos = "0 6 -14",
        [string]$CamRot = "18 0 0",
        [string]$SunRot = "50 -35 0",
        # 한때 40~50으로 올렸던 적이 있는데 그건 오진이었다. 화면이 검었던 것은
        # 강도 단위 때문이 아니라 캡처 전에 창을 리사이즈했고 SSGI 패스가 그것을
        # 견디지 못했기 때문이다. 리사이즈를 빼고 나니 원래 값으로 정상이다.
        [double]$SunIntensity = 1.6
    )
    return @(
        "object.create MainCamera Camera",
        "component.add MainCamera CameraComponent",
        "object.transform MainCamera $CamPos $CamRot",
        "object.create SunLight Light",
        "component.add SunLight LightComponent",
        "object.transform SunLight 0 20 0 $SunRot",
        "object.property SunLight LightComponent m_lightType DirectionalLight",
        "object.property SunLight LightComponent m_intencity $SunIntensity",
        "object.property SunLight LightComponent m_color 1,0.96,0.9,1"
    )
}

# 임포트한 모델을 배치하고 이름을 바꾼다.
#
# model.place는 glTF 노드 이름 그대로 오브젝트를 만든다. 같은 도형을 두 번째로
# 놓으면 엔진이 이름 충돌을 피하려고 "Prim_Cube (1)"처럼 번호를 붙이므로,
# 몇 번째 배치인지를 알아야 이름을 제대로 잡을 수 있다. 그래서 Occurrence를
# 받는다 — 안 그러면 두 번째부터 조용히 이름이 안 바뀌고, 저장된 씬에만
# 흔적이 남는다(실측으로 겪었다).
function Place-Primitive {
    param([string]$Kind, [string]$Name, [string]$Transform, [int]$Occurrence = 0)

    $placed = if ($Occurrence -eq 0) { "Prim_$Kind" } else { "Prim_$Kind ($Occurrence)" }
    return @(
        "model.place Prim_$Kind",
        "wait 2",
        "object.rename $placed $Name",
        "object.transform $Name $Transform"
    )
}

# ── 씬 정의 ──
#
# 각 씬이 무엇을 확인하려는 것인지 이름과 주석에 남긴다.
$scenes = @(

    @{
        Name = "FT_Primitives"
        Why  = "도형 임포트 자체 — 메시·UV·노멀·재질이 엔진까지 오는가"
        Body = {
            $c = @()
            $c += Import-Models @("Plane","Cube","Sphere","IcoSphere","Cylinder","Cone","Torus","Suzanne")
            $c += New-CameraAndSun -CamPos "0 4 -12" -CamRot "12 0 0"
            $c += Place-Primitive "Plane"     "Ground"    "0 0 0 0 0 0 1 1 1"
            # 한 줄로 세운다. 크기가 통일돼 있어 줄만 맞추면 비교가 쉽다.
            $c += Place-Primitive "Cube"      "P_Cube"      "-5.25 0.5 0 0 0 0 1 1 1"
            $c += Place-Primitive "Sphere"    "P_Sphere"    "-3.75 0.5 0 0 0 0 1 1 1"
            $c += Place-Primitive "IcoSphere" "P_IcoSphere" "-2.25 0.5 0 0 0 0 1 1 1"
            $c += Place-Primitive "Cylinder"  "P_Cylinder"  "-0.75 0.5 0 0 0 0 1 1 1"
            $c += Place-Primitive "Cone"      "P_Cone"      "0.75 0.5 0 0 0 0 1 1 1"
            $c += Place-Primitive "Torus"     "P_Torus"     "2.25 0.5 0 90 0 0 1 1 1"
            $c += Place-Primitive "Suzanne"   "P_Suzanne"   "3.75 0.6 0 0 180 0 1 1 1"
            $c += "script.add FT_Primitives PackageSmokeProbe"
            return $c
        }
    },

    @{
        Name = "FT_Shadow"
        Why  = "그림자 — 캐스케이드가 거리에 따라 갈리는지 보려고 깊이 방향으로 늘어놓는다"
        Body = {
            $c = @()
            $c += Import-Models @("Plane","Cube","Torus","Cylinder","Suzanne")
            # 카메라를 낮고 멀리 둔다. 그림자는 지면에 누워 있어 시선이 낮아야 보인다.
            $c += New-CameraAndSun -CamPos "0 5 -16" -CamRot "14 0 0" -SunRot "55 -40 0"
            $c += Place-Primitive "Plane" "Ground" "0 0 0 0 0 0 1 1 1"

            # 깊이 방향으로 5개. 캐스케이드 분할이 43/105/500쯤에서 갈리므로
            # 가까운 것과 먼 것이 다른 캐스케이드에 들어간다.
            $c += Place-Primitive "Cube"     "Caster_00" "-2 1 2   0 20 0 2 2 2"
            $c += Place-Primitive "Torus"    "Caster_01" "2 1.5 8  90 0 0 3 3 3"
            $c += Place-Primitive "Cylinder" "Caster_02" "-3 2 18  0 0 0 2 4 2"
            $c += Place-Primitive "Suzanne"  "Caster_03" "3 2 34   0 180 0 4 4 4"
            # 큐브는 이 씬에서 두 번째 배치라 Occurrence 1이다.
            $c += Place-Primitive "Cube"     "Caster_04" "0 3 60   0 45 0 6 6 6" -Occurrence 1
            return $c
        }
    },

    @{
        Name = "FT_Material"
        Why  = "재질 상수 — metallic·roughness가 실제로 셰이더에 닿는가(구 5x2 격자)"
        Body = {
            # 격자는 블렌더에서 값을 넣어 한 파일로 뽑았다. 엔진 CLI로 재질 값을
            # 바꾸려면 MeshRenderer 안의 Material까지 파고들어야 하는데, 저작
            # 도구에서 실어 보내면 그 경로가 통째로 필요 없고 '한 파일에 여러
            # 재질' 임포트 경로도 같이 확인된다.
            $c = @()
            $c += Import-Models @("Plane","MatGrid")
            $c += New-CameraAndSun -CamPos "0 2 -11" -CamRot "6 0 0" -SunIntensity 2.0
            $c += Place-Primitive "Plane" "Ground" "0 -2 0 0 0 0 1 1 1"
            $c += Place-Primitive "MatGrid" "MaterialGrid" "0 0 0 0 0 0 1 1 1"
            return $c
        }
    },

    @{
        Name = "FT_Lights"
        Why  = "광원 종류 — 방향광·점광·스포트가 각각 다르게 보이는가"
        Body = {
            $c = @()
            $c += Import-Models @("Plane","Sphere","Cube")
            $c += New-CameraAndSun -CamPos "0 7 -13" -CamRot "24 0 0" -SunIntensity 0.35
            $c += Place-Primitive "Plane" "Ground" "0 0 0 0 0 0 1 1 1"

            # 광원마다 아래에 물체를 둔다. 빈 바닥만으로는 감쇠 모양이 잘 안 보인다.
            $c += Place-Primitive "Sphere" "Under_Point" "-4 1 0 0 0 0 2 2 2"
            $c += Place-Primitive "Cube"   "Under_Spot"  "4 1 0 0 30 0 2 2 2"

            $c += @(
                "object.create PointLight Light",
                "component.add PointLight LightComponent",
                "object.transform PointLight -4 4 -2 0 0 0",
                "object.property PointLight LightComponent m_lightType PointLight",
                "object.property PointLight LightComponent m_color 1,0.35,0.25,1",
                "object.property PointLight LightComponent m_intencity 6",
                "object.property PointLight LightComponent m_range 14",

                "object.create SpotLight Light",
                "component.add SpotLight LightComponent",
                # 스포트는 오브젝트의 +Z가 조사 방향이다(LightComponent가 회전에서 만든다).
                "object.transform SpotLight 4 7 -2 70 0 0",
                "object.property SpotLight LightComponent m_lightType SpotLight",
                "object.property SpotLight LightComponent m_color 0.35,0.6,1,1",
                "object.property SpotLight LightComponent m_intencity 8",
                "object.property SpotLight LightComponent m_range 20",
                "object.property SpotLight LightComponent m_spotLightAngle 45"
            )
            return $c
        }
    }
)

# ── 실행 ──
$built = @()
$failed = @()

foreach ($scene in $scenes) {
    if ($Only.Count -gt 0 -and $Only -notcontains $scene.Name) { continue }

    $scenePath = Join-Path $SceneDir ($scene.Name + ".creator")
    $candidatePath = Join-Path $SceneDir `
        ("." + $scene.Name + ".$PID.candidate.creator")
    $cmdFile = Join-Path $workDir ($scene.Name + ".txt")

    $commands = @("scene.new $($scene.Name)", "wait 20")
    $commands += & $scene.Body
    $commands += @(
        "wait 20",
        "scene.dump $($scene.Name)",
        "scene.save $candidatePath",
        "wait 10",
        "quit"
    )

    Set-Content -Path $cmdFile -Value ($commands -join "`n") -NoNewline -Encoding UTF8

    if (Test-Path -LiteralPath $candidatePath) {
        Remove-Item -LiteralPath $candidatePath -Force
    }

    Write-Host ""
    Write-Host "[$($scene.Name)] $($scene.Why)"

    $proc = Start-Process -FilePath $Exe -ArgumentList "--script `"$cmdFile`"" `
        -WorkingDirectory (Split-Path -Parent $Exe) -PassThru -NoNewWindow
    $exited = $proc.WaitForExit($TimeoutSec * 1000)
    if (-not $exited) {
        # 종료가 멈추는 기존 문제가 있다. 씬 파일만 나왔으면 저작 자체는 끝난
        # 것이므로, 강제 종료하고 파일 유무로 판정한다.
        try { $proc | Stop-Process -Force } catch {}
        Start-Sleep -Milliseconds 500
    }

    if (Test-Path -LiteralPath $candidatePath) {
        # 생성물이 단순히 "존재"하는지만 보면 구 GameObject 스키마가 다시
        # 출력돼도 저작 성공으로 오판한다. feature-test scene의 정본은 현재
        # serializer이므로, 최소 구조를 여기서 함께 고정한다.
        $sceneText = Get-Content -LiteralPath $candidatePath -Raw
        $schemaErrors = @()
        if ($sceneText -notmatch '(?m)^m_Entities:\s*$') {
            $schemaErrors += 'm_Entities 없음'
        }
        if ($sceneText -match '(?m)^m_SceneObjects:\s*$') {
            $schemaErrors += '구 m_SceneObjects 잔존'
        }
        if ($sceneText -match '(?m)^\s+m_transform:\s*$') {
            $schemaErrors += '구 Entity.m_transform 잔존'
        }
        if ($sceneText -match '(?m)^\s+m_gameObjectType:\s*') {
            $schemaErrors += '구 m_gameObjectType 잔존'
        }

        if ($scene.Name -eq 'FT_Primitives') {
            $entityCount = [regex]::Matches(
                $sceneText, '(?m)^\s{2}- Entity:\s+\d+\s*$').Count
            $transformCount = [regex]::Matches(
                $sceneText, '(?m)^\s{6}- Transform:\s+\d+\s*$').Count
            $meshCount = [regex]::Matches(
                $sceneText, '(?m)^\s{6}- MeshRenderer:\s+\d+\s*$').Count
            $cameraCount = [regex]::Matches(
                $sceneText, '(?m)^\s{6}- CameraComponent:\s+\d+\s*$').Count
            $lightCount = [regex]::Matches(
                $sceneText, '(?m)^\s{6}- LightComponent:\s+\d+\s*$').Count
            $scriptCount = [regex]::Matches(
                $sceneText, '(?m)^\s{6}- ScriptComponent:\s+\d+\s*$').Count

            if ($entityCount -ne 11 -or $transformCount -ne 11 -or
                $meshCount -ne 8 -or $cameraCount -ne 1 -or
                $lightCount -ne 1 -or $scriptCount -ne 1 -or
                $sceneText -notmatch '(?m)^\s+m_scriptType:\s*PackageSmokeProbe\s*$') {
                $schemaErrors += "구성 불일치(Entity=$entityCount Transform=$transformCount Mesh=$meshCount Camera=$cameraCount Light=$lightCount Script=$scriptCount)"
            }
        }

        if ($schemaErrors.Count -gt 0) {
            Write-Host "  현행 스키마 검증 실패: $($schemaErrors -join ', ')" -ForegroundColor Red
            Remove-Item -LiteralPath $candidatePath -Force
            $failed += $scene.Name
            continue
        }

        if (Test-Path -LiteralPath $scenePath) {
            $backupPath = "$scenePath.rollback.$PID"
            [IO.File]::Replace($candidatePath, $scenePath, $backupPath, $true)
            if (Test-Path -LiteralPath $backupPath) {
                Remove-Item -LiteralPath $backupPath -Force
            }
        } else {
            [IO.File]::Move($candidatePath, $scenePath)
        }
        $bytes = (Get-Item -LiteralPath $scenePath).Length
        Write-Host ("  저장·현행 스키마 검증 완료: {0} ({1:N0} 바이트)" -f $scene.Name, $bytes) -ForegroundColor Green
        $built += $scene.Name
    } else {
        Write-Host "  candidate 저장 실패(기존 정본 보존): $candidatePath" -ForegroundColor Red
        $failed += $scene.Name
    }
}

Write-Host ""
Write-Host "저작 완료 $($built.Count)개 · 실패 $($failed.Count)개"
if ($failed.Count -gt 0) {
    Write-Host "실패한 씬: $($failed -join ', ')" -ForegroundColor Red
    exit 1
}
exit 0
