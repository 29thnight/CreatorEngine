# 트랜스폼 값 왕복 대조 (SceneGraphRedesignPlan §4 트랙 S — S1-b 선행 게이트)
#
# ── 이 검사가 메우는 구멍 ──
#
# 역직렬화기는 스키마 쪽 이름으로만 값을 당겨 오고 YAML의 키를 열거하지 않는다.
# 그래서 필드를 다른 곳으로 옮기면(S1-b: GameObject::m_transform → 컴포넌트 블록)
# 승격 경로가 빠진 자리에서 위치·회전·크기가 **에러 없이 사라진다**.
#
# 기존 세트는 이걸 못 잡는다:
#   - prefab_roundtrip : 인스턴스·등록 **개수**만 본다. 값은 안 본다.
#   - authored_rects   : 값을 대조하지만 대상이 UI(RectTransform)뿐이다.
#   - reflect_golden   : 기본 생성 타입의 덤프라 저작 데이터의 왕복은 안 본다.
#
# ── 판정 항목 ──
#
#   1  before/after 다이제스트가 둘 다 있다   — 시나리오가 도중에 멈추지 않았다
#   2  오브젝트 수가 최소치 이상               — 빈 씬을 재고 통과하지 않는다(README 원칙)
#   3  줄 단위 diff 0                          — ★ 값이 왕복을 건넜다
#   4  해시 일치                               — 3의 독립 확인
#
# ── 재료는 CLI로 저작한다 (2026-08-20 이전) ──
#
# 저작 자산(Test1.creator)에 기대던 것을 끊었다. 자산은 전부 폐기 대상이라(§0.05)
# 그대로 두면 폐기와 함께 이 자가 죽는다. 지금은 검사 스크립트가 object.create/
# parent/transform 명령을 생성해 씬을 그 자리에서 짓는다 — 아래 저작 블록 참고.
#
# 실행: pwsh -NoProfile -File Tools\regression\verify-transform-roundtrip.ps1
param(
    [string]$Exe = "C:\Users\lance\source\CreatorEngine\x64\Debug\CreatorEditor.exe",
    [string]$Work = $env:TEMP,
    [int]$TimeoutSeconds = 300,
    # 저작할 오브젝트 수. 하한 판정(MinObjects)이 이 값에서 나온다 — 0이면 검사가
    # 눈을 감은 것이고, 너무 낮으면 "기본 씬을 재고 통과"를 못 잡는다
    # (실측: 경로가 틀렸을 때 3개였다).
    [int]$ObjectCount = 40
)

$exeDir = [System.IO.Path]::GetDirectoryName($Exe)
if (-not (Test-Path $Exe)) { "실행 파일이 없다: $Exe"; exit 1 }

$template = Join-Path $PSScriptRoot "transform_roundtrip.txt"
if (-not (Test-Path $template)) { "시나리오가 없다: $template"; exit 1 }

# ── 저작 블록 생성 (2026-08-20, 자산·게이트 CLI 이전) ──
#
# 예전에는 저작 자산 Test1.creator(68 노드)를 열어 그 값을 재료로 삼았다. 저작
# 자산은 전부 폐기 대상이므로(§0.05) 그 자를 CLI 저작본 위로 옮긴다.
#
# ★ 값은 오브젝트마다 **서로 달라야 한다.** 같은 값이 반복되면 자리가 뒤바뀌어도
#   다이제스트가 같아서, 순서가 무너지는 결함을 통과시킨다. 저작 씬을 쓰던 시절에는
#   그 분포가 우연에 맡겨져 있었고 확인할 방법도 없었다. 여기서는 인덱스로 구동해
#   위치·회전·크기 아홉 성분이 모두 달라지게 만든다.
#
# ★ 회전은 오일러 각(도)이고 엔진이 사원수로 바꿔 저장한다(Cmd_object_transform).
#   360의 배수나 짐벌 경계(±90)에 걸리면 왕복에서 표현이 갈릴 수 있으므로 그 근처를
#   피해 잘게 흩는다.
#
# ★ 계층을 만든다 — 부모가 있으면 로컬/월드가 갈리고, 그 갈림이 왕복에서 유지되는지가
#   이 검사의 핵심 중 하나다(S1-b가 옮긴 것이 바로 그 값들이다). 3의 배수마다
#   앞선 오브젝트의 자식으로 붙여 2~3단 깊이를 만든다.
$author = @()
for ($i = 0; $i -lt $ObjectCount; $i++) {
    $name = "TR_Obj$i"
    $author += "object.create $name Empty"

    # 계층: 0,3,6…은 최상위로 두고 나머지는 바로 앞 3의 배수의 자식이 된다.
    if (($i % 3) -ne 0) {
        $parentIndex = [int]([math]::Floor($i / 3)) * 3
        $author += "object.parent $name TR_Obj$parentIndex"
    }

    # 아홉 성분 전부 서로 다르게. 소수 둘째 자리까지만 쓴다 — 부동소수 표기가
    # 왕복에서 자릿수 때문에 흔들리는 것과 값이 실제로 유실되는 것을 섞지 않기 위해서다.
    $px = [math]::Round((($i * 1.37) - 12.5), 2)
    $py = [math]::Round((($i * -0.83) + 4.25), 2)
    $pz = [math]::Round((($i * 2.11) - 6.75), 2)
    $rx = [math]::Round(((($i * 7) % 71) - 35), 2)
    $ry = [math]::Round(((($i * 13) % 83) - 41), 2)
    $rz = [math]::Round(((($i * 17) % 67) - 33), 2)
    $sx = [math]::Round((0.6 + (($i * 3) % 17) * 0.05), 2)
    $sy = [math]::Round((0.7 + (($i * 5) % 13) * 0.07), 2)
    $sz = [math]::Round((0.8 + (($i * 7) % 11) * 0.09), 2)

    $author += "object.transform $name $px $py $pz $rx $ry $rz $sx $sy $sz"

    # 명령이 쌓이는 동안 한 번씩 프레임을 넘긴다 — 생성·부모지정·트랜스폼이
    # 한 프레임에 몰려도 문제는 없지만, 중간 상태가 로그에 남아 진단이 쉬워진다.
    if (($i % 10) -eq 9) { $author += "wait 5" }
}
$authorBlock = ($author -join "`n")

# 하한: 저작한 수보다 적게 나오면 저작 자체가 실패한 것이다. 씬 루트가 하나 더
# 잡힐 수 있으므로 저작 수 자체를 하한으로 쓴다.
$MinObjects = $ObjectCount

# 경로는 실행 직전에 채운다 — 절대 경로를 시나리오에 박으면 다른 작업 폴더에서
# 조용히 실패하고, 그 실패가 '오브젝트 3개'라는 엉뚱한 모습으로 나타난다.
$tempScene = Join-Path $Work "TransformRoundTrip.creator"
$tmpPath  = ($tempScene) -replace '\\', '/'
if (Test-Path $tempScene) { Remove-Item $tempScene -Force }

$scenario = Join-Path $Work "transform_roundtrip_resolved.txt"
((Get-Content $template -Raw) -replace '\{\{AUTHOR_BLOCK\}\}', $authorBlock) -replace '\{\{TMP_SCENE\}\}', $tmpPath |
    Set-Content $scenario -Encoding UTF8

$outFile = Join-Path $Work "transform_roundtrip.out"
$errFile = Join-Path $Work "transform_roundtrip.err"

$proc = Start-Process -FilePath $Exe -ArgumentList "--script", $scenario `
    -WorkingDirectory $exeDir `
    -RedirectStandardOutput $outFile -RedirectStandardError $errFile -PassThru
$proc.WaitForExit($TimeoutSeconds * 1000) | Out-Null
if (-not $proc.HasExited) {
    $proc.Kill()
    "실패: 시간 초과 ($TimeoutSeconds 초)"
    exit 1
}

$lines = Get-Content $outFile

$before = @($lines | Where-Object { $_ -match '^\[tfdigest:before\] ' } | ForEach-Object { $_ -replace '^\[tfdigest:before\] ', '' })
$after  = @($lines | Where-Object { $_ -match '^\[tfdigest:after\] '  } | ForEach-Object { $_ -replace '^\[tfdigest:after\] ',  '' })

# 마지막 줄은 "합계 …" 요약이다 — 본문과 분리한다.
$beforeSummary = $before | Where-Object { $_ -match '^합계 ' } | Select-Object -Last 1
$afterSummary  = $after  | Where-Object { $_ -match '^합계 ' } | Select-Object -Last 1
$beforeRows = @($before | Where-Object { $_ -notmatch '^합계 ' })
$afterRows  = @($after  | Where-Object { $_ -notmatch '^합계 ' })

if (-not $beforeSummary -or -not $afterSummary) {
    "실패: 다이제스트가 둘 다 나오지 않았다 (before=$($beforeRows.Count)줄, after=$($afterRows.Count)줄)"
    "  시나리오가 도중에 멈췄을 수 있다. 출력: $outFile"
    exit 1
}

if ($beforeRows.Count -lt $MinObjects) {
    "실패: 오브젝트가 너무 적다 ($($beforeRows.Count) < $MinObjects) — 검사가 빈 씬을 재고 통과할 뻔했다"
    exit 1
}

$failed = $false

if ($beforeRows.Count -ne $afterRows.Count) {
    "실패: 오브젝트 수가 다르다 (저장 전 $($beforeRows.Count) vs 재로드 후 $($afterRows.Count))"
    $failed = $true
}

$limit = [Math]::Min($beforeRows.Count, $afterRows.Count)
$diffCount = 0
for ($i = 0; $i -lt $limit; $i++) {
    if ($beforeRows[$i] -ne $afterRows[$i]) {
        $diffCount++
        if ($diffCount -le 10) {
            "  줄 $($i + 1): 저장 전 '$($beforeRows[$i])'"
            "           재로드 후 '$($afterRows[$i])'"
        }
    }
}

if ($diffCount -gt 0) {
    "실패: 트랜스폼 값이 왕복을 못 건넜다 ($diffCount 줄 다름 / $limit 줄)"
    if ($diffCount -gt 10) { "  (앞 10건만 표시)" }
    $failed = $true
}

$beforeHash = if ($beforeSummary -match '해시 ([0-9a-f]+)') { $Matches[1] } else { "?" }
$afterHash  = if ($afterSummary  -match '해시 ([0-9a-f]+)') { $Matches[1] } else { "?" }
if ($beforeHash -ne $afterHash) {
    "실패: 다이제스트 해시가 다르다 (저장 전 $beforeHash vs 재로드 후 $afterHash)"
    $failed = $true
}

if (Test-Path $tempScene) { Remove-Item $tempScene -Force }

if ($failed) { exit 1 }

"전체 통과 — 트랜스폼 값 $($beforeRows.Count)개 오브젝트가 저장·재로드를 건넜다 (해시 $beforeHash)"
exit 0
