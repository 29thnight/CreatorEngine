[CmdletBinding()]
param(
    # 관측이 상한보다 낮을 때만 쓴다. 이관이 진행됐을 때 눈금을 내린다.
    [switch]$UpdateRatchet
)

# PHASE 14.5 LC9 (§18) — 자동화 소비자가 사람용 문자열을 판정에 쓰지 않는가.
#
# ── 닫으려는 문장 ───────────────────────────────────────────────────────
#
# §18: "자동화 consumer 가 source/help/한국어 verdict 문자열을 파싱하지 않는다."
#
# 이 저장소에서 그 습관이 실제로 비용을 냈다. `Invoke-Dx12Suite.ps1` 하나가 같은
# 자리에서 세 번 틀렸다 — help 를 읽어 35 중 26 만 돌렸고, 등록 형태가 바뀌자
# 35 를 0 으로 읽었고, 판정 어휘를 통과/실패 둘로만 읽어 계측 검사(`완료`)를
# 실패로 셌다. 앞의 둘은 LC3 이 `commands.list` 로 닫았고, 셋째를 LC9 가
# `--result-format jsonl` 로 닫았다.
#
# ── 왜 0 이 아니라 래칫인가 ─────────────────────────────────────────────
#
# 오늘 0 이 아니기 때문이다. 남은 소비자들은 각자 자기 도메인의 stdout 마커를
# 읽고 있고, 그것을 한 슬라이스에서 전부 옮기는 것은 계획이 금지한 종류의
# 일괄 변경이다(§15). 눈금을 박아 **늘지 않게** 하고, 옮길 때마다 내린다.
#
# ★ 주석은 세지 않는다. 이 검사를 넣은 커밋이 바로 그 이유로 자기 자신을
#   붉게 만들었다 — `Invoke-Dx12Suite.ps1` 의 LC9 주석이 "예전에는 이 정규식을
#   썼다" 며 그 정규식을 인용한다. 고칠 수 없는 것(설명)을 눈금에 넣으면
#   래칫이 0 에 닿지 못한다.

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$repoRoot    = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
$ratchetPath = Join-Path $PSScriptRoot 'cli_consumer.ratchet.json'

if (-not (Test-Path -LiteralPath $ratchetPath -PathType Leaf)) {
    "래칫이 없다: $ratchetPath (-UpdateRatchet 으로 최초 기록)"
    $ratchet = [pscustomobject]@{
        schema = 'lc9-consumer-ratchet/1'
        recorded = (Get-Date -Format 'yyyy-MM-dd')
        maxVerdictParsers = 9999
        note = ''
        files = @()
    }
} else {
    $ratchet = Get-Content -LiteralPath $ratchetPath -Raw | ConvertFrom-Json
}

# 판정 어휘를 **정규식·매칭 문맥에서** 쓰는 줄만 센다. 사람에게 보여 주려고
# `Write-Host "통과"` 하는 것은 소비가 아니라 출력이다.
$verdictPattern = '(통과|실패|완료|어서션|무판정)'
$parseContext   = '(-match|-notmatch|Select-String|\[regex\]|-split|IndexOf|Contains)'

$offenders = @()
foreach ($file in Get-ChildItem -Path (Join-Path $repoRoot 'Tools') -Recurse -Filter '*.ps1') {
    $lines = [IO.File]::ReadAllLines($file.FullName)
    $hits = @()
    for ($i = 0; $i -lt $lines.Count; ++$i) {
        $line = $lines[$i]

        # ★ 주석 제외. 위 머리말 참고 — 설명이 눈금에 들어가면 0 에 닿지 못한다.
        if ($line -match '^\s*#') { continue }

        # 판정 어휘가 매칭 문맥 안에 있을 때만 소비다.
        if ($line -match $verdictPattern -and $line -match $parseContext) {
            $hits += ("  {0}:{1}" -f $file.Name, ($i + 1))
        }
    }
    if ($hits.Count -gt 0) {
        $offenders += [pscustomobject]@{
            File = $file.FullName.Substring($repoRoot.Length).TrimStart('\', '/').Replace('\', '/')
            Hits = $hits
        }
    }
}

$count = $offenders.Count
"{0,-26} {1} 파일 / 상한 {2}" -f 'verdict-parsers', $count, $ratchet.maxVerdictParsers
foreach ($offender in ($offenders | Sort-Object File)) {
    "  $($offender.File)"
}

$failures = New-Object System.Collections.Generic.List[string]

if ($count -gt $ratchet.maxVerdictParsers) {
    $failures.Add(("verdict-parsers : 사람용 판정 문자열을 파싱하는 소비자가 늘었다 " +
        "$count > $($ratchet.maxVerdictParsers). 새 소비자는 --result-format jsonl 을 읽어라 " +
        "(§18: 자동화 consumer 가 한국어 verdict 문자열을 파싱하지 않는다)"))
}
elseif ($count -lt $ratchet.maxVerdictParsers) {
    if ($UpdateRatchet) {
        $ratchet.maxVerdictParsers = $count
        $ratchet.recorded = (Get-Date -Format 'yyyy-MM-dd')
        $ratchet.files = @($offenders | Sort-Object File | ForEach-Object { $_.File })
        $ratchet | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath $ratchetPath -Encoding UTF8
        "래칫을 $count 로 내렸다 — 이 변경을 커밋에 함께 담을 것"
    }
    else {
        "  (상한보다 $($ratchet.maxVerdictParsers - $count) 낮다 — -UpdateRatchet 으로 눈금을 내릴 것)"
    }
}

# ── 무인증 경로 0 (§8 · §14.1) ──────────────────────────────────────────
#
# ★ 토큰 검사보다 **먼저** 답하는 라우트가 하나라도 생기면, 웹페이지가 로컬
#   에디터를 조작할 수 있다. §8 이 "`GET /health` 조차 토큰을 요구한다"고 못
#   박은 이유이고, 그 성질은 코드 순서 하나에 달려 있다 — 라우트 분기가
#   `Authorized(...)` 앞에 오면 그 순간 무너진다.
#
#   그래서 순서를 본다. `HandleRequest` 안에서 첫 `request.path` 분기가
#   `Authorized` 호출보다 뒤에 있어야 한다.
$serviceSource = Join-Path $repoRoot 'Engine\CommandService\CommandService.cpp'
$serviceText = [IO.File]::ReadAllText($serviceSource)
$handleIndex = $serviceText.IndexOf('std::string Service::HandleRequest')
if ($handleIndex -lt 0) {
    $failures.Add('auth-order : HandleRequest 를 찾지 못했다 — 이 검사가 무엇을 보는지 알 수 없다')
}
else {
    $body = $serviceText.Substring($handleIndex)
    $authAt = $body.IndexOf('Authorized(request)')

    # ★ 라우트 분기는 **Yoda 비교**로 쓰여 있다(`"/health" == request.path`).
    #
    #   처음에는 `request.path ==` 만 찾았고, 그래서 이 검사는 라우트를 **하나도
    #   못 찾은 채** 통과했다 — 무인증 경로가 있어도 초록이었다. 검사가 자기가
    #   아무것도 안 보고 있다는 것을 말하지 않으면 그 검사는 없는 것만 못하다.
    #   그래서 두 어순을 다 찾고, **하나도 못 찾으면 실패**로 만든다.
    $routeMatches = [regex]::Matches($body,
        '(request\.path\s*==|==\s*request\.path|request\.path\.rfind)')
    $routeAt = if ($routeMatches.Count -gt 0) { $routeMatches[0].Index } else { -1 }

    "{0,-26} Authorized@{1} firstRoute@{2} routes={3}" -f `
        'auth-order', $authAt, $routeAt, $routeMatches.Count

    if ($authAt -lt 0) {
        $failures.Add('auth-order : HandleRequest 안에 Authorized(request) 호출이 없다 — 무인증 경로다')
    }
    if ($routeMatches.Count -eq 0) {
        $failures.Add('auth-order : 라우트 분기를 하나도 찾지 못했다 — 이 검사가 아무것도 보고 있지 않다(형태가 바뀌었으면 패턴을 고쳐라)')
    }
    elseif ($routeAt -lt $authAt) {
        $failures.Add('auth-order : 토큰 검사보다 앞서는 라우트 분기가 있다 — 그 경로는 무인증이다(§8)')
    }
}

""
if ($failures.Count -gt 0) {
    "실패:"
    $failures | ForEach-Object { "  - $_" }
    exit 1
}
"소비자 계약 통과 — 판정 파서 상한 유지 · 무인증 경로 0"
exit 0
