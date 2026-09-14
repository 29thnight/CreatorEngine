[CmdletBinding()]
param(
    [string]$Dashboard = (Join-Path $PSScriptRoot '..\..\docs\RefactoringPlanDashboard.html'),

    # 위반을 몇 개까지 나열할지. 한 줄이 깨지면 그 뒤로 줄줄이 딸려 나오므로
    # 전수를 찍으면 정작 첫 원인이 묻힌다.
    [int]$ListLimit = 12,

    # node 가 없으면 '전체 파싱' 단정을 돌 수 없다. 기본값은 그 사실을 **명시해서**
    # 보고만 하고 통과시킨다(아래 구조 단정은 node 없이도 전부 돈다). CI 처럼
    # node 가 반드시 있어야 하는 자리에서는 이 스위치로 실패로 승격한다.
    [switch]$RequireFullParse
)

# 계획 대시보드(docs/RefactoringPlanDashboard.html)가 열리는 상태인지 본다.
#
# ── 왜 필요한가 (2026-09-14 실제 사고) ──
#
#   `4cc9b30d`(PBR-W7)가 `TASKS` 배열의 note 값 안에 큰따옴표 여섯을 raw 로 넣었다.
#   그 줄은 JS 문자열 안이라 문자열이 일찍 닫히고 배열 전체가 파싱에 실패했다 —
#   **페이지가 통째로 비어 보인다.** 슬라이스마다 이 파일을 손으로 고치므로 같은
#   사고는 반복된다. 그때까지 검사는 '눈으로 본다'는 습관뿐이었고, 계획서에는
#   "dashboard JavaScript 검사를 통과했다"고 적혀 있었지만 저장소에 그런 자가
#   없었다(`git grep RefactoringPlanDashboard` — 문서만 걸린다).
#
# ── 왜 정규식이 아니라 상태 기계인가 ──
#
#   사고 당일 `키: "값"` 정규식으로 훑었더니 **거짓 양성 623건**이 나왔다. 한 줄에
#   필드가 여럿이라 `.*` 가 필드 경계를 넘는다. 그래서 여기서는 문자 단위로
#   문자열 상태를 추적한다.
#
#   ★ 함정 하나. "줄이 끝났는데 문자열 안이면 실패"로 잡으면 **이번 사고를 못
#     잡는다.** raw 큰따옴표가 짝수(여섯)여서 줄 끝에서는 문자열 밖으로 돌아온다.
#     그래서 단정을 **값의 경계**에 건다: 문자열이 닫힌 다음 글자는 `,`·`}`·`]`
#     여야 하고, 문자열이 열리기 전 글자는 `:`·`,`·`[`·`{` 여야 한다. 일찍 닫힌
#     문자열은 이 규약을 반드시 어긴다.
#
# ── 어느 바이너리를 재는가 ──
#
#   아무것도 재지 않는다. 문서 파일 하나만 읽으므로 -Exe 를 받지 않는다.

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

if (-not (Test-Path -LiteralPath $Dashboard -PathType Leaf)) {
    "대시보드 파일이 없다: $Dashboard"
    exit 1
}

$failures = New-Object System.Collections.Generic.List[string]
$text = [IO.File]::ReadAllText($Dashboard)

# ── 1. TASKS 배열을 찾고 문자열 경계를 검사한다 ────────────────────────────────

$anchor = 'const TASKS = ['
$anchorAt = $text.IndexOf($anchor)
if ($anchorAt -lt 0) {
    "'$anchor' 선언을 못 찾았다 — 이름이 바뀌었다면 이 게이트도 함께 고쳐야 한다"
    exit 1
}

$open = $text.IndexOf('[', $anchorAt)
$chars = $text.ToCharArray()
$total = $chars.Length

# 줄 번호는 배열 앞부분을 한 번만 세어 두고 이어서 센다(전수 IndexOf 는 느리다).
$line = 1
for ($i = 0; $i -lt $open; $i++) { if ($chars[$i] -eq "`n") { $line++ } }

$openBefore = @(':', ',', '[', '{', '(')
$closeAfter = @(',', '}', ']')

$violations = New-Object System.Collections.Generic.List[string]
$entrySpans = New-Object System.Collections.Generic.List[object]

$bracket = 0
$brace = 0
$entryStart = -1
$arrayEnd = -1
$quote = [char]0
$inString = $false
$stringOpenLine = 0

for ($i = $open; $i -lt $total; $i++) {
    $c = $chars[$i]
    if ($c -eq "`n") { $line++ }

    if ($inString) {
        if ($c -eq '\') { $i++; if ($i -lt $total -and $chars[$i] -eq "`n") { $line++ }; continue }
        if ($c -ne $quote) { continue }

        # 닫혔다. 다음 글자가 값의 경계여야 한다.
        $inString = $false
        $j = $i + 1
        while ($j -lt $total -and [char]::IsWhiteSpace($chars[$j])) { $j++ }
        if ($j -ge $total -or $closeAfter -notcontains $chars[$j]) {
            $seen = if ($j -lt $total) { $chars[$j] } else { '<끝>' }
            $violations.Add("$line 줄: 문자열이 닫힌 뒤에 ',' · '}' · ']' 가 아니라 '$seen' 이 왔다 — 값 안의 따옴표가 문자열을 일찍 닫았다")
        }
        continue
    }

    # ★ 주석을 건너뛰지 않으면 주석 안의 따옴표를 값으로 읽는다. 첫 판이 그래서
    #   거짓 양성 814건을 냈다 — TASKS 사이사이에 `// 구 4-5 "DLSS 구상" …` 같은
    #   구분 주석이 있다. 문자열 밖일 때만 주석으로 본다(URL 의 `//` 는 문자열 안이다).
    if ($c -eq '/' -and $i + 1 -lt $total) {
        $next = $chars[$i + 1]
        if ($next -eq '/') {
            while ($i -lt $total -and $chars[$i] -ne "`n") { $i++ }
            $line++
            continue
        }
        if ($next -eq '*') {
            $i += 2
            while ($i + 1 -lt $total -and -not ($chars[$i] -eq '*' -and $chars[$i + 1] -eq '/')) {
                if ($chars[$i] -eq "`n") { $line++ }
                $i++
            }
            $i++
            continue
        }
    }

    switch ($c) {
        '`' {
            $violations.Add("$line 줄: 백틱(템플릿 리터럴)은 TASKS 안에서 쓰지 않는다 — 이 게이트의 전제를 깬다")
        }
        { $_ -eq '"' -or $_ -eq "'" } {
            $inString = $true
            $quote = $c
            $stringOpenLine = $line
            $j = $i - 1
            while ($j -ge $open -and [char]::IsWhiteSpace($chars[$j])) { $j-- }
            if ($j -lt $open -or $openBefore -notcontains $chars[$j]) {
                $seen = if ($j -ge $open) { $chars[$j] } else { '<앞>' }
                $violations.Add("$stringOpenLine 줄: 문자열이 ':' · ',' · '[' · '{' 뒤가 아니라 '$seen' 뒤에서 열렸다 — 앞 값이 일찍 닫힌 흔적이다")
            }
        }
        '[' { $bracket++ }
        ']' {
            $bracket--
            if ($bracket -eq 0) { $arrayEnd = $i; $i = $total }
        }
        '{' {
            if ($bracket -eq 1 -and $brace -eq 0) { $entryStart = $i }
            $brace++
        }
        '}' {
            $brace--
            if ($bracket -eq 1 -and $brace -eq 0 -and $entryStart -ge 0) {
                $entrySpans.Add([pscustomobject]@{ Text = $text.Substring($entryStart, $i - $entryStart + 1) })
                $entryStart = -1
            }
        }
    }
}

if ($inString) {
    $violations.Add("$stringOpenLine 줄에서 열린 문자열이 닫히지 않았다")
}
if ($arrayEnd -lt 0) {
    $violations.Add('TASKS 배열이 닫히지 않았다 — 대괄호 균형이 0으로 돌아오지 않는다')
}

if ($violations.Count -gt 0) {
    $failures.Add("TASKS 문자열/구조 위반 $($violations.Count)건")
}

# ── 2. 항목 모양 ──────────────────────────────────────────────────────────────
#
# 빈 집합을 성공으로 읽지 않기 위해 "항목이 하나라도 있다"를 먼저 단정한다.

$entries = New-Object System.Collections.Generic.List[object]
$shapeErrors = New-Object System.Collections.Generic.List[string]
$knownStatus = @('todo', 'progress', 'done', 'blocked', 'stopped')

foreach ($span in $entrySpans) {
    $body = $span.Text
    $idM = [regex]::Match($body, 'id:\s*"([^"]*)"')
    $stM = [regex]::Match($body, 'status:\s*"([^"]*)"')
    $dayM = [regex]::Match($body, 'days:\s*(-?[\d.]+)')
    $phM = [regex]::Match($body, 'phase:\s*(?:"([^"]*)"|(-?[\d.]+))')
    $earnM = [regex]::Match($body, 'earnedDays:\s*(-?[\d.]+)')

    $id = if ($idM.Success) { $idM.Groups[1].Value } else { '<id 없음>' }
    if (-not $idM.Success) { $shapeErrors.Add("id 가 없는 항목이 있다: $($body.Substring(0, [Math]::Min(80, $body.Length)))") ; continue }
    if (-not $stM.Success) { $shapeErrors.Add("$id : status 가 없다"); continue }
    if (-not $dayM.Success) { $shapeErrors.Add("$id : days 가 없다"); continue }
    if (-not $phM.Success) { $shapeErrors.Add("$id : phase 가 없다"); continue }
    if ($knownStatus -notcontains $stM.Groups[1].Value) {
        $shapeErrors.Add("$id : 모르는 status '$($stM.Groups[1].Value)' — ST_LABEL/ST_CLASS 에 없으면 행이 깨져 그려진다")
        continue
    }

    $entries.Add([pscustomobject]@{
            Id     = $id
            Status = $stM.Groups[1].Value
            Days   = [double]$dayM.Groups[1].Value
            Phase  = if ($phM.Groups[1].Success) { $phM.Groups[1].Value } else { $phM.Groups[2].Value }
            IsNum  = -not $phM.Groups[1].Success
            Earned = if ($earnM.Success) { [double]$earnM.Groups[1].Value } else { $null }
        })
}

if ($entries.Count -eq 0) {
    $failures.Add('TASKS 항목을 하나도 못 읽었다 — 배열이 비었거나 이 게이트의 항목 인식이 틀렸다')
}
if ($shapeErrors.Count -gt 0) {
    $failures.Add("항목 모양 위반 $($shapeErrors.Count)건")
}

# ── 3. phase-meta 문장의 산수 ─────────────────────────────────────────────────
#
# 2026-09-14 에 PHASE 4 의 '진행 기성 3.5일 · 잔여 4일' 이 실제 데이터(3.75·3.75)와
# 어긋나 있었다. 문장이 스스로 쓴 항을 데이터로 검산한다.
#
# ★ 문장 양식은 페이즈마다 다르다. '활성 N행' 을 쓴 문장만 대상으로 삼고,
#   그 수가 0이면 **면제가 아니라 실패**로 본다 — 감사 대상이 통째로 비는 모양을
#   초록으로 읽지 않기 위해서다.

$metaMatches = [regex]::Matches($text, 'class="phase-meta">(.*?)</div>', [Text.RegularExpressions.RegexOptions]::Singleline)
$tableMatches = [regex]::Matches($text, '<table class="tasks" data-phase="([\d.]+)"')

$metaChecked = 0
$metaErrors = New-Object System.Collections.Generic.List[string]

foreach ($tm in $tableMatches) {
    $phase = $tm.Groups[1].Value
    $best = $null
    foreach ($mm in $metaMatches) {
        if ($mm.Index -lt $tm.Index -and ($null -eq $best -or $mm.Index -gt $best.Index)) { $best = $mm }
    }
    if ($null -eq $best) { continue }
    $sentence = [regex]::Replace($best.Groups[1].Value, '<[^>]*>', '')

    $rowsM = [regex]::Match($sentence, '활성\s*(\d+)행')
    if (-not $rowsM.Success) { continue }
    $metaChecked++

    # Measure-Object 는 빈 입력에서 아무것도 내지 않아 StrictMode 아래서 터진다.
    # 손으로 더한다.
    $rows = @($entries | Where-Object { $_.IsNum -and [double]$_.Phase -eq [double]$phase -and $_.Status -ne 'stopped' })
    $days = 0.0
    $doneDays = 0.0
    $progEarned = 0.0
    foreach ($r in $rows) {
        $days += $r.Days
        if ($r.Status -eq 'done') { $doneDays += $r.Days }
        elseif ($null -ne $r.Earned) { $progEarned += $r.Earned }
    }

    $claim = {
        param($pattern)
        $m = [regex]::Match($sentence, $pattern)
        if ($m.Success) { [double]$m.Groups[1].Value } else { $null }
    }
    $claimDays = & $claim '활성\s*\d+행\s*([\d.]+)일'
    $claimDone = & $claim '완료\s*([\d.]+)일'
    $claimProg = & $claim '진행 기성\s*([\d.]+)일'
    $claimLeft = & $claim '잔여\s*([\d.]+)일'

    # 문장이 '진행 기성' 항을 쓰지 않으면 잔여에서도 그것을 빼지 않는다(페이즈마다
    # 문장 모양이 다르다 — 문장 자신의 모양을 따른다).
    $expectLeft = $days - $doneDays - $(if ($null -eq $claimProg) { 0.0 } else { $progEarned })

    $cmp = {
        param($name, $claimed, $actual)
        if ($null -eq $claimed) { return }
        if ([Math]::Abs($claimed - $actual) -ge 1e-9) {
            $metaErrors.Add("PHASE $phase phase-meta: $name 주장=$claimed 실제=$actual")
        }
    }
    & $cmp '행' ([double]$rowsM.Groups[1].Value) ([double]$rows.Count)
    & $cmp '일' $claimDays $days
    & $cmp '완료' $claimDone $doneDays
    & $cmp '진행 기성' $claimProg $progEarned
    & $cmp '잔여' $claimLeft $expectLeft
}

if ($metaChecked -eq 0) {
    $failures.Add("'활성 N행' 을 쓴 phase-meta 문장이 하나도 없다 — 문장 양식이 바뀌었다면 이 게이트도 함께 고쳐야 한다")
}
if ($metaErrors.Count -gt 0) {
    $failures.Add("phase-meta 산수 불일치 $($metaErrors.Count)건")
}

# ── 4. 전체 파싱(node 가 있을 때만) ───────────────────────────────────────────
#
# 위 단정은 TASKS 만 본다. 같은 파일의 렌더 코드가 깨지는 것은 실제 파서라야 잡는다.

$fullParse = 'absent'
$node = Get-Command node -ErrorAction SilentlyContinue
if ($null -ne $node) {
    $helper = Join-Path ([IO.Path]::GetTempPath()) ("verify-plan-dashboard-{0}.js" -f ([guid]::NewGuid().ToString('N')))
    $js = @'
const fs = require('fs');
const vm = require('vm');
const file = process.argv[2];
const html = fs.readFileSync(file, 'utf8');
const re = /<script\b[^>]*>([\s\S]*?)<\/script>/gi;
let m, index = 0, bad = 0;
while ((m = re.exec(html)) !== null) {
  index += 1;
  const line = html.slice(0, m.index).split('\n').length;
  try {
    new vm.Script(m[1], { filename: `script${index}` });
  } catch (e) {
    bad += 1;
    console.log(`script ${index} (${line}줄 시작): 파싱 실패 — ${e.message}`);
  }
}
console.log(`scriptBlocks=${index} broken=${bad}`);
process.exit(bad === 0 ? 0 : 1);
'@
    try {
        [IO.File]::WriteAllText($helper, $js, (New-Object Text.UTF8Encoding($false)))
        $output = & $node.Source $helper $Dashboard 2>&1
        $nodeExit = $LASTEXITCODE
        $output | ForEach-Object { "  $_" }
        if ($nodeExit -eq 0) {
            $fullParse = 'ran'
        }
        else {
            $fullParse = 'broken'
            $failures.Add('node 전체 파싱 실패 — script 블록이 깨졌다')
        }
    }
    finally {
        if (Test-Path -LiteralPath $helper) { Remove-Item -LiteralPath $helper -Force }
    }
}
elseif ($RequireFullParse) {
    $failures.Add('node 가 없어 전체 파싱 단정을 돌지 못했다(-RequireFullParse)')
}

# ── 보고 ─────────────────────────────────────────────────────────────────────

"entries=$($entries.Count) stringViolations=$($violations.Count) shapeErrors=$($shapeErrors.Count) metaChecked=$metaChecked/$($tableMatches.Count) metaMismatch=$($metaErrors.Count) fullParse=$fullParse"

if ($violations.Count -gt 0) {
    'TASKS 문자열/구조 위반:'
    $violations | Select-Object -First $ListLimit | ForEach-Object { "  $_" }
    if ($violations.Count -gt $ListLimit) { "  ... 외 $($violations.Count - $ListLimit)건 (한 줄이 깨지면 뒤가 줄줄이 딸려 나온다 — 첫 줄부터 고쳐라)" }
}
if ($shapeErrors.Count -gt 0) {
    '항목 모양 위반:'
    $shapeErrors | Select-Object -First $ListLimit | ForEach-Object { "  $_" }
}
if ($metaErrors.Count -gt 0) {
    'phase-meta 산수 불일치:'
    $metaErrors | ForEach-Object { "  $_" }
}

if ($fullParse -eq 'absent') {
    '주의: node 가 없어 전체 파싱 단정은 돌지 않았다 — TASKS 구조 단정만 잰 결과다'
}

if ($failures.Count -gt 0) {
    ''
    '실패:'
    $failures | ForEach-Object { "  - $_" }
    exit 1
}

"전체 통과 — TASKS $($entries.Count)항목이 온전하고 phase-meta $metaChecked 문장의 산수가 데이터와 맞는다"
exit 0
