# Edit → Play → Stop 왕복 구조 대조 (E3 착수 전 필수 게이트)
#
# 왜 필요한가
# ───────────
# E3는 play-mode 소유권을 SceneManager에서 Editor로 옮긴다. 그런데 회귀 세트 60여 종
# 어디에도 재생 왕복이 씬 구조를 보존하는지 재는 검사가 없었다 — 있는 것은 UI 텍스트
# 로그 통과 횟수 같은 프록시뿐이라, 재생 후 엔티티가 사라지거나 계층이 뒤집혀도
# 전부 초록으로 남는다. 이관을 시작하기 전에 "지금 동작"을 먼저 못 박아야 옮긴 뒤에
# "동작이 같다"를 주장할 수 있다.
#
# ── 판정 항목 ──
#
#   1  측정이 실제로 나왔다      — 세 지점의 digest가 모두 파싱되고 오브젝트 수가 0이 아니다
#   2  엔진이 정상 종료했다      — 크래시하고도 통과하는 게이트가 이 저장소에 실재했다
#   3  재생 진입이 실제로 일어났다 — play.state의 gameStart/editorSceneLoaded가 전이한다
#   4  ★ 정지 후 편집 상태로 복원 — 오브젝트 수와 해시가 재생 전과 정확히 일치
#
# 3번이 핵심이다. 이것이 없으면 "재생을 아예 안 했는데 상태가 그대로라 통과"라는
# 거짓 통과가 성립한다 — 이 저장소가 이미 여러 번 겪은 양식이다.
#
# 처음에는 "재생 중 digest가 편집 상태와 다르다"로 3번을 재려 했는데 그건 틀린
# 관측이다. 재생 진입은 좌표를 바꾸지 않고 m_scenePhase만 바꾸므로, 스크립트가
# 무언가를 움직이지 않는 한 digest는 같다. 그래서 상태 플래그를 직접 본다.
param(
    [string]$Exe = (Join-Path $PSScriptRoot "..\..\Bin\x64-Debug\Editor\CreatorEditor.exe"),
    [string]$Work = ""
)

$ErrorActionPreference = "Stop"
$repoRoot = Split-Path (Split-Path $PSScriptRoot -Parent) -Parent

if (-not (Test-Path -LiteralPath $Exe)) {
    throw "Editor executable is missing: $Exe"
}

if ([string]::IsNullOrWhiteSpace($Work)) {
    $Work = Join-Path ([IO.Path]::GetTempPath()) ("CE_PlayRoundtrip_" + [guid]::NewGuid().ToString("N"))
}
New-Item -ItemType Directory -Force -Path $Work | Out-Null

try {
    $commandFile = Join-Path $Work "commands.txt"
    $stdout = Join-Path $Work "stdout.txt"
    $stderr = Join-Path $Work "stderr.txt"

    # 시작 씬이 뜬 뒤 편집 상태를 찍고, 재생 → 몇 프레임 → 재생 중 상태 → 정지 →
    # 몇 프레임 → 복원 상태를 찍는다. play/stop은 SetGameStart만 세우고 실제 씬 구조
    # 변경은 다음 프레임의 ApplyPendingSceneStructureChange가 하므로 wait가 필수다.
    # 재생 중에 오브젝트를 하나 만든다. 이것이 이 게이트의 핵심이다 — 아무것도
    # 바꾸지 않고 재생 전후를 비교하면 "복원했다"가 아니라 "건드린 게 없다"를 재게
    # 된다. 정지가 백업에서 되살리는 것이 맞다면 이 오브젝트는 사라져야 한다.
    $spawnName = "CE_PlayProbe_Spawned"
    [IO.File]::WriteAllLines($commandFile, @(
        "wait 5"
        "play.state"
        "scene.transformdigest edit"
        "play"
        "wait 5"
        "object.create $spawnName"
        "wait 3"
        "play.state"
        "scene.transformdigest playing"
        "stop"
        "wait 5"
        "play.state"
        "scene.transformdigest restored"
        "quit"
    ))

    $process = Start-Process -FilePath $Exe `
        -ArgumentList "--console", "--script", $commandFile `
        -WorkingDirectory (Split-Path $Exe -Parent) `
        -RedirectStandardOutput $stdout -RedirectStandardError $stderr -PassThru
    if (-not $process.WaitForExit(180000)) {
        $process.Kill()
        throw "play round-trip probe timed out"
    }
    $process.WaitForExit()
    if ($process.ExitCode -ne 0) {
        $errorText = if (Test-Path -LiteralPath $stderr) {
            Get-Content -LiteralPath $stderr -Raw
        } else { "" }
        throw "play round-trip probe exited with $($process.ExitCode): $errorText"
    }

    $output = Get-Content -LiteralPath $stdout -Raw

    # 엔진이 찍는 해시는 **열거 순서에 민감**하다. 그런데 재생 왕복은 엔티티를
    # 파괴하고 백업에서 되살리므로 슬롯 인덱스가 재배정된다(실측: Main Camera와
    # Directional Light가 1↔2로 뒤바뀐다). 내용은 동일한데 해시만 달라지므로,
    # 엔진 해시를 그대로 비교하면 "복원 실패"로 오판한다.
    #
    # 계획서가 요구하는 속성은 "scene·hierarchy가 복원된다"이지 "슬롯 번호가
    # 같다"가 아니다. 그래서 행을 이름 기준으로 정렬해 내용 집합을 비교하고,
    # 슬롯 순서는 실패시키지 않되 PASS 줄에 남겨 변화가 눈에 보이게 한다.
    function Get-Digest([string]$label) {
        $summaryPattern = '\[tfdigest:' + [regex]::Escape($label) +
            '\] 합계 오브젝트 (\d+) · 해시 ([0-9a-f]{16})'
        $summary = [regex]::Match($output, $summaryPattern)
        if (-not $summary.Success) {
            throw "digest '$label' was not emitted — the probe did not reach that point"
        }

        $rowPattern = '\[tfdigest:' + [regex]::Escape($label) + '\] (\d+\|[^\r\n]+)'
        $rows = @([regex]::Matches($output, $rowPattern) | ForEach-Object {
            $_.Groups[1].Value
        })
        # index 를 뗀 내용만 남긴다 — 슬롯 재배정은 복원 실패가 아니다.
        $content = @($rows | ForEach-Object { ($_ -split '\|', 2)[1] } | Sort-Object)
        $order = @($rows | ForEach-Object { ($_ -split '\|')[1] }) -join ','

        return [pscustomobject]@{
            Label = $label
            Count = [int]$summary.Groups[1].Value
            Hash  = $summary.Groups[2].Value
            Content = ($content -join "`n")
            Order = $order
            RowCount = $rows.Count
        }
    }

    $edit = Get-Digest "edit"
    $playing = Get-Digest "playing"
    $restored = Get-Digest "restored"

    $states = @([regex]::Matches($output,
        '\[play\.state\] gameStart=(\d) paused=\d editorSceneLoaded=(\d) pending=(\d) entities=(\d+)') |
        ForEach-Object {
            [pscustomobject]@{
                GameStart = [int]$_.Groups[1].Value
                EditorSceneLoaded = [int]$_.Groups[2].Value
                Pending = [int]$_.Groups[3].Value
                Entities = [int]$_.Groups[4].Value
            }
        })
    if ($states.Count -ne 3) {
        throw "expected 3 play.state samples, got $($states.Count)"
    }

    # 1: 빈 측정을 성공으로 읽지 않는다.
    if ($edit.Count -le 0) {
        throw "edit digest measured 0 objects — nothing was compared"
    }

    # 3: 재생 전이가 실제로 일어났는가. 이 단정이 없으면 'play가 무시됐는데 상태가
    #    그대로라 통과'가 성립한다. 좌표가 아니라 상태 플래그를 본다.
    if ($states[0].GameStart -ne 0) {
        throw "scene was already in play mode before the probe pressed play"
    }
    if ($states[1].GameStart -ne 1 -or $states[1].EditorSceneLoaded -ne 1) {
        throw ("play did not take effect: gameStart=$($states[1].GameStart) " +
            "editorSceneLoaded=$($states[1].EditorSceneLoaded) — " +
            "the restore assertion below would be vacuous")
    }
    if ($states[2].GameStart -ne 0 -or $states[2].EditorSceneLoaded -ne 0) {
        throw ("stop did not take effect: gameStart=$($states[2].GameStart) " +
            "editorSceneLoaded=$($states[2].EditorSceneLoaded)")
    }
    if ($states[2].Pending -ne 0) {
        throw "a scene structure change was still pending after stop settled"
    }

    # 행이 실제로 파싱됐는지 확인한다 — 빈 집합을 비교해 '차이 0'으로 통과하는
    # 양식을 이 저장소가 이미 겪었다.
    if ($edit.RowCount -ne $edit.Count -or $restored.RowCount -ne $restored.Count) {
        throw ("digest rows were not fully parsed: edit=$($edit.RowCount)/$($edit.Count) " +
            "restored=$($restored.RowCount)/$($restored.Count)")
    }

    # 3-b: 재생 중 생성이 실제로 반영됐는가. 이것이 없으면 아래 복원 단정이
    #      "아무것도 안 바꾸고 같은지 봤다"가 된다.
    if ($playing.Count -ne ($edit.Count + 1)) {
        throw ("the probe object was not created during play: edit=$($edit.Count) " +
            "playing=$($playing.Count) — the restore assertion would be vacuous")
    }
    if ($playing.Content -notmatch [regex]::Escape($spawnName)) {
        throw "the spawned probe object is missing from the playing digest"
    }

    # 4: 정지 후 편집 상태로 복원 — 오브젝트 수와 내용 집합이 정확히 일치해야 한다.
    #    재생 중 만든 오브젝트는 백업에 없으므로 사라져야 한다.
    if ($restored.Content -match [regex]::Escape($spawnName)) {
        throw "an object created during play survived stop — the scene was not restored from the backup"
    }
    if ($restored.Count -ne $edit.Count) {
        throw ("stop did not restore the object count: edit=$($edit.Count) " +
            "restored=$($restored.Count)")
    }
    if ($restored.Content -ne $edit.Content) {
        throw ("stop did not restore the scene content.`nedit:`n$($edit.Content)`n" +
            "restored:`n$($restored.Content)")
    }

    # 슬롯 재배정은 실패시키지 않되 눈에 보이게 남긴다. E3가 이 성질을 바꾸면
    # PASS 줄의 표기가 달라져 로그에서 드러난다.
    $orderNote = if ($restored.Order -eq $edit.Order) { "stable" } else {
        "reassigned($($edit.Order) -> $($restored.Order))"
    }
    "play round-trip: PASS (objects=$($edit.Count), content=restored, slots=$orderNote)"
}
finally {
    if (Test-Path -LiteralPath $Work) {
        $verified = [IO.Path]::GetFullPath($Work)
        $tempBase = [IO.Path]::GetFullPath([IO.Path]::GetTempPath())
        if ($verified.StartsWith($tempBase, [StringComparison]::OrdinalIgnoreCase) -and
            (Split-Path $verified -Leaf).StartsWith(
                "CE_PlayRoundtrip_", [StringComparison]::OrdinalIgnoreCase)) {
            Remove-Item -LiteralPath $verified -Recurse -Force -ErrorAction SilentlyContinue
        }
    }
}
