# UI 레이아웃 회귀 검사 — 런타임 계산 결과가 저작된 배치를 재현하는지 본다.
#
# PHASE 7-2에서 m_worldRect를 직렬화 대상에서 뺐다. 그래서 런타임의 worldRect는
# 앵커·sizeDelta·pivot과 부모 rect만으로 처음부터 다시 계산된 값이다. 반면 프리팹
# 파일에는 예전에 저장된 m_worldRect가 그대로 남아 있고, 그것이 에디터가 실제로
# 배치했던 결과다. 둘이 같으면 레이아웃 재현이 정확하다는 뜻이다.
#
# 파일에 남은 m_worldRect는 이제 로드되지 않으므로(모르는 키는 무시된다) 여기서만
# 정답지로 쓰인다. 에셋을 다시 저장하면 이 키가 사라지고 검사 대상도 함께 줄어든다.
#
# 이름으로 짝을 짓지 않는 이유: 같은 이름이 프리팹 안에서도(range_select (1)~(7))
# 프리팹 사이에서도 겹쳐서, 이름 기반 대조는 값이 맞는데도 엉뚱하게 짝지어 실패한다.
# 값 다중집합이 같은지만 보면 배치가 통째로 재현됐는지를 이름과 무관하게 판정할 수 있다.
#
# 사용법:
#   pwsh Tools/regression/verify-authored-rects.ps1 -Prefabs Canvas,HPCanvas
#   (생략하면 아래 기본 목록 12종을 검사한다)
param(
    [string]$Exe = "C:\Users\lance\source\CreatorEngine\x64\Debug\Academy_4Q.exe",
    [string]$PrefabDir = "C:\Users\lance\source\CreatorEngine\Dynamic_CPP\Assets\Prefabs",
    # 임시 스크립트·출력이 나가는 곳. 저장소 안(=$PSScriptRoot)을 기본값으로 두었더니
    # verify_*.txt / .err 26개가 Tools/regression에 쌓였다.
    [string]$Work = $env:TEMP,
    [string[]]$Prefabs = @(
        'Canvas', 'HPCanvas', 'PauseBox', 'BossHPBar', 'TutorialUI', 'SettingCanvas',
        'ItemPopupCanvas', 'UI_CanvasesIngame', 'MonHPCanvas', 'EmoteCanvas',
        'BootstrapCanvas', 'MenuSettingCanvas')
)

function Get-AuthoredRects([string]$path) {
    $result = @()
    foreach ($line in Get-Content $path) {
        if ($line -match 'm_worldRect:\s*\{x:\s*([-\d.]+),\s*y:\s*([-\d.]+),\s*width:\s*([-\d.]+),\s*height:\s*([-\d.]+)\}') {
            # ui.rect는 static_cast<int>로 찍는다 = 0 방향 절삭. Floor를 쓰면 음수에서
            # -142.5가 -143이 되어 런타임의 -142와 어긋난다(실제로 이 차이만 3건 떴다).
            $result += ("{0},{1},{2},{3}" -f [Math]::Truncate([double]$matches[1]), [Math]::Truncate([double]$matches[2]),
                                             [Math]::Truncate([double]$matches[3]), [Math]::Truncate([double]$matches[4]))
        }
    }
    return $result
}

function Get-RuntimeRects([string]$logPath) {
    $result = @()
    foreach ($line in ((Get-Content $logPath -Raw) -split "`n")) {
        if ($line -match '\[ui\.rect\].*world\(([-\d]+),\s*([-\d]+),\s*([-\d]+),\s*([-\d]+)\)') {
            $result += ("{0},{1},{2},{3}" -f $matches[1], $matches[2], $matches[3], $matches[4])
        }
    }
    return $result
}

$exeDir = [System.IO.Path]::GetDirectoryName($Exe)
$failures = 0

foreach ($prefab in $Prefabs) {
    $script = Join-Path $Work "verify_$prefab.txt"
    @"
wait 90
prefab.instantiate $prefab
wait 90
ui.rect *
wait 20
quit
"@ | Set-Content $script

    $before = Get-Date
    $proc = Start-Process -FilePath $Exe -ArgumentList "--script", $script `
        -WorkingDirectory $exeDir `
        -RedirectStandardOutput (Join-Path $Work "verify_$prefab.out") `
        -RedirectStandardError (Join-Path $Work "verify_$prefab.err") -PassThru
    $proc.WaitForExit(180000) | Out-Null
    if (-not $proc.HasExited) { $proc.Kill(); "$prefab : TIMEOUT"; $failures++; continue }

    $log = Get-ChildItem (Join-Path $exeDir "Log\Editor_*.html") |
           Where-Object { $_.LastWriteTime -ge $before } | Sort-Object LastWriteTime -Descending | Select-Object -First 1
    if (-not $log) { "$prefab : 로그 없음"; $failures++; continue }

    # 캔버스 루트는 화면 크기가 구동하므로(7-1) 앵커 계산 대상이 아니다. 양쪽에서 똑같이 뺀다.
    $isCanvasRoot = { $_ -eq '-960,-540,1920,1080' }
    $authored = @((Get-AuthoredRects (Join-Path $PrefabDir "$prefab.prefab") | Where-Object { -not (& $isCanvasRoot) }) | Sort-Object)
    $runtime = @((Get-RuntimeRects $log.FullName | Where-Object { -not (& $isCanvasRoot) }) | Sort-Object)

    # 한쪽이 비면 Compare-Object가 null/빈 배열을 거부하며 에러를 내고, $diff가 null이
    # 되어 아래 카운트가 0·0으로 계산된다 — 저작 N개 vs 런타임 0개가 "일치"로 둔갑하는
    # 거짓 통과다(2026-08-20 실제 발생). 비교 전에 빈 쪽을 명시적으로 판정한다.
    if ($authored.Count -eq 0 -or $runtime.Count -eq 0) {
        if ($authored.Count -eq $runtime.Count) {
            # 저작 정답지(m_worldRect)가 없는 프리팹 — 재저장으로 키가 사라진 경우(머리말 참조)
            "{0,-22} 일치 (0개 rect — 저작 정답지 없음)" -f $prefab
        } else {
            $failures++
            "{0,-22} 불일치 — 저작 {1}개 / 런타임 {2}개 (빈 쪽이 있어 값 비교 불가 — 로그에 ui.rect 출력이 없거나 정답지가 사라졌다)" -f `
                $prefab, $authored.Count, $runtime.Count
        }
        continue
    }

    $diff = Compare-Object $authored $runtime
    $onlyAuthored = ($diff | Where-Object { $_.SideIndicator -eq '<=' }).Count
    $onlyRuntime  = ($diff | Where-Object { $_.SideIndicator -eq '=>' }).Count

    if (0 -eq $onlyAuthored -and 0 -eq $onlyRuntime) {
        "{0,-22} 일치 ({1}개 rect)" -f $prefab, $authored.Count
    } else {
        $failures++
        "{0,-22} 불일치 — 저작 {1}개 / 런타임 {2}개 · 저작에만 {3} · 런타임에만 {4}" -f `
            $prefab, $authored.Count, $runtime.Count, $onlyAuthored, $onlyRuntime
        $diff | Select-Object -First 12 | ForEach-Object { "    {0} {1}" -f $_.SideIndicator, $_.InputObject }
    }
}

if ($failures -gt 0) { "실패 $failures / $($Prefabs.Count)"; exit 1 }
"전체 통과 ($($Prefabs.Count)개 프리팹)"
exit 0
