# 리플렉션 속성 배치 규약 (docs/design/CodingConventions.md §7.4)
#
# 무엇을 지키는가
# ─────────────
# 변수·메서드에 다는 [[reflgen::…]]·[[creator::…]] 는 자기 줄에 있고 선언은 그 아래 줄에 있다. 속성을 단 선언
# (바로 위 설명 주석 포함)은 앞뒤 내용과 빈 줄로 갈라져 있다. 범위의 경계({·}·접근 지정자)와는 가르지 않는다.
#
# 왜 필요한가
# ──────────
# reflgen 전환 뒤 반영 범위는 opt-out 이다 — 필드는 [[reflgen::ignore]] 를 달지 않는 한 저장된다. 속성이 선언과
# 한 줄에 붙어 있으면 무엇이 저장에서 빠졌는지가 긴 타입 이름 사이에 묻혀 리뷰에서 보이지 않는다. 속성을 자기 줄에
# 두면 줄 머리만 훑어도 보인다. 규약을 주석으로만 적으면 다음 사람이 한 줄로 다시 붙인다 — 도는 검사만이
# 강제력을 갖는다. 고치는 것은 도구가 한다(--apply).
#
# ⚠ 통과가 곧 검증은 아니다
# ────────────────────────
# 검사 대상 헤더를 못 찾으면 0건으로 통과한다. 그래서 ① 변환 규칙을 합성 표본으로 먼저 확인하고(--self-test),
# ② 속성을 가진 헤더가 실제로 모였는지 본다.
param(
    [string]$Python = 'python'
)
$ErrorActionPreference = 'Stop'
$repoRoot = Split-Path (Split-Path $PSScriptRoot -Parent) -Parent
$tool = Join-Path $repoRoot 'Tools\migration\reflgen_attribute_layout.py'

& $Python $tool --self-test
if ($LASTEXITCODE -ne 0) { "FAIL: 배치 도구의 자기 시험이 실패했다"; exit 1 }

$output = & $Python $tool --check
$status = $LASTEXITCODE
$output
# 여러 줄이면 배열이다 — 배열에 -notmatch 를 걸면 거르기가 되므로 한 문자열로 합쳐 본다.
if (($output -join "`n") -notmatch 'headers with reflection attributes: ([1-9]\d*)') {
    "FAIL: 리플렉션 속성을 가진 헤더를 하나도 찾지 못했다 — 빈 집합 위의 통과다"
    exit 1
}
if ($status -ne 0) { "FAIL: 규약과 다른 배치가 있다"; exit 1 }
"PASS"
exit 0
