# 자산 신원 프로필 `ce.uuidv8.sha256.v1` (MBC1)
#
# 세 갈래 독립 유도가 같은 값을 내는지 한 실행에서 잰다:
#   ① 제품 C++ — `assets.identity` selftest(FIPS KAT·BCrypt 대조·Python 벡터·fail-closed·registry)
#   ② Python hashlib — Tools/regression/asset_identity_vectors.json(생성기가 낸 정답)
#   ③ .NET SHA256 — 이 스크립트가 §2.2 바이트 계약을 **여기서 다시 조립**해 계산
#
# ★ ①이 ②와 같다는 것만 보면 "둘이 같은 (틀린) 규약을 공유"할 수 있다(experiment.anim
#   D4e-1의 눈먼 초록). ③은 언어·해시 구현·바이트 조립 코드가 전부 다른 세 번째 유도다.
#   셋이 벡터 15건 전부에서 같아야 통과다.
param(
    [string]$Exe = (Join-Path $PSScriptRoot "..\..\Bin\x64-Debug\Editor\CreatorEditor.exe"),
    [string]$Work = $env:TEMP,
    [int]$TimeoutSeconds = 180
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

if (-not (Test-Path -LiteralPath $Exe -PathType Leaf)) { "실행 파일이 없다: $Exe"; exit 1 }
$Exe = (Resolve-Path -LiteralPath $Exe).Path
$root = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
$vectorsPath = Join-Path $PSScriptRoot 'asset_identity_vectors.json'
if (-not (Test-Path -LiteralPath $vectorsPath -PathType Leaf)) { "벡터 파일이 없다: $vectorsPath"; exit 1 }
$vectors = Get-Content -LiteralPath $vectorsPath -Raw -Encoding UTF8 | ConvertFrom-Json

$failures = New-Object System.Collections.Generic.List[string]

# ── ③ .NET 독립 유도 ───────────────────────────────────────────────────────────
# ★ 함수가 배열을 돌려줄 때 `,`로 감싼다 — PowerShell은 원소 1개 배열을 스칼라로 언롤해
#   `.Length`가 사라진다(stableKey "c" 한 글자가 정확히 그 경우다).
function ConvertFrom-Hex([string]$hex) {
    $bytes = New-Object byte[] ($hex.Length / 2)
    for ($i = 0; $i -lt $bytes.Length; $i++) { $bytes[$i] = [Convert]::ToByte($hex.Substring($i * 2, 2), 16) }
    return ,$bytes
}
function Get-U32BE([int]$n) {
    # 각 원소를 괄호로 묶는다 — `,`가 `-band`보다 먼저 묶여 배열에 -band를 거는 사고가 났다.
    return ,[byte[]]@((($n -shr 24) -band 0xFF), (($n -shr 16) -band 0xFF), (($n -shr 8) -band 0xFF), ($n -band 0xFF))
}
function Get-Utf8Nfc([string]$s) {
    if (-not $s.IsNormalized([Text.NormalizationForm]::FormC)) { throw "벡터 문자열이 NFC가 아니다: $s" }
    return ,[Text.Encoding]::UTF8.GetBytes($s)
}
function Build-IdentityInput($v) {
    $out = New-Object System.Collections.Generic.List[byte]
    $out.AddRange([Text.Encoding]::ASCII.GetBytes([string]$v.profile))
    $out.Add([byte]0)
    $fields = New-Object System.Collections.Generic.List[byte[]]
    $fields.Add([byte[]](Get-Utf8Nfc $v.domain)); $fields.Add([byte[]](ConvertFrom-Hex $v.namespaceHex))
    $fields.Add([byte[]](Get-Utf8Nfc $v.kind));   $fields.Add([byte[]](Get-Utf8Nfc $v.stableKey))
    foreach ($field in $fields) {
        $out.AddRange([byte[]](Get-U32BE $field.Length)); $out.AddRange($field)
    }
    return ,$out.ToArray()
}
function Get-UuidV8([byte[]]$inputBytes) {
    $sha = [System.Security.Cryptography.SHA256]::Create()
    try { $digest = $sha.ComputeHash([byte[]]$inputBytes) } finally { $sha.Dispose() }
    $u = $digest[0..15]
    $u[6] = ($u[6] -band 0x0F) -bor 0x80
    $u[8] = ($u[8] -band 0x3F) -bor 0x80
    $hex = -join ($u | ForEach-Object { $_.ToString('x2') })
    return @{ uuid = "$($hex.Substring(0,8))-$($hex.Substring(8,4))-$($hex.Substring(12,4))-$($hex.Substring(16,4))-$($hex.Substring(20,12))"
              sha  = -join ($digest | ForEach-Object { $_.ToString('x2') }) }
}

$dotnetUuids = @{}
$vectorCount = 0
foreach ($v in $vectors.profileVectors) {
    $vectorCount++
    $inputBytes = Build-IdentityInput $v
    $inputHex = -join ($inputBytes | ForEach-Object { $_.ToString('x2') })
    if ($inputHex -ne $v.inputHex) { $failures.Add(".NET 입력 바이트열 ≠ Python: $($v.name)") }
    $derived = Get-UuidV8 $inputBytes
    if ($derived.sha -ne $v.sha256Hex) { $failures.Add(".NET SHA-256 ≠ Python: $($v.name)") }
    if ($derived.uuid -ne $v.uuid) { $failures.Add(".NET UUID ≠ Python: $($v.name) $($derived.uuid) vs $($v.uuid)") }
    if ($v.uuid -cnotmatch '^[0-9a-f]{8}-[0-9a-f]{4}-8[0-9a-f]{3}-[89ab][0-9a-f]{3}-[0-9a-f]{12}$') {
        $failures.Add("벡터 UUID가 canonical v8 표기가 아니다: $($v.name) $($v.uuid)")
    }
    $dotnetUuids[[string]$v.name] = $derived.uuid
}
if ($vectorCount -lt 15) { $failures.Add("벡터가 15건 미만이다: $vectorCount") }
foreach ($s in $vectors.shaVectors) {
    $sha = [System.Security.Cryptography.SHA256]::Create()
    try { $d = -join ($sha.ComputeHash((ConvertFrom-Hex $s.inputHex)) | ForEach-Object { $_.ToString('x2') }) } finally { $sha.Dispose() }
    if ($d -ne $s.sha256Hex) { $failures.Add(".NET KAT ≠ 공표값: $($s.name)") }
}
# 변이가 발화하는지 — 프로필 한 글자 변이 벡터는 원본과 달라야 한다.
$alpha = $vectors.profileVectors | Where-Object { $_.name -eq 'model.alpha' }
$v0 = $vectors.profileVectors | Where-Object { $_.name -eq 'mutation.profile-v0' }
if (-not $alpha -or -not $v0 -or $alpha.uuid -eq $v0.uuid) { $failures.Add('프로필 변이 벡터가 원본과 같다(변이 검출 미발화)') }

# ── ① 제품 selftest ───────────────────────────────────────────────────────────
$run = Join-Path $Work ("CE_AssetIdentity_" + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $run -Force | Out-Null
$scenario = Join-Path $run 'commands.txt'
$stdout = Join-Path $run 'stdout.txt'
$stderr = Join-Path $run 'stderr.txt'
@('assets.identity', 'quit') | Set-Content -LiteralPath $scenario -Encoding UTF8

$process = Start-Process -FilePath $Exe -ArgumentList @('--script', $scenario) `
    -WorkingDirectory $root -WindowStyle Hidden `
    -RedirectStandardOutput $stdout -RedirectStandardError $stderr -PassThru
$process.WaitForExit($TimeoutSeconds * 1000) | Out-Null
if (-not $process.HasExited) { $process.Kill(); "TIMEOUT output=$run"; exit 1 }

$text = if (Test-Path -LiteralPath $stdout) { Get-Content -LiteralPath $stdout -Raw -Encoding UTF8 } else { '' }
$cliPass = ([regex]::Matches($text, '\[CLI\] assets\.identity 통과')).Count
$summary = [regex]::Match($text, '단정 (\d+)건 중 통과 (\d+) · 실패 (\d+)')
$assertions = 0; $assertFailed = -1
if ($summary.Success) { $assertions = [int]$summary.Groups[1].Value; $assertFailed = [int]$summary.Groups[3].Value }
if ($cliPass -ne 1) { $failures.Add("[CLI] assets.identity 통과가 1회가 아니다: $cliPass") }
if (-not $summary.Success) { $failures.Add('단정 요약 줄이 없다') }
elseif ($assertFailed -ne 0) { $failures.Add("selftest 단정 실패 $assertFailed 건") }
elseif ($assertions -lt 150) { $failures.Add("단정 수가 너무 적다($assertions) — 검사 범위가 줄었다") }

# C++가 찍은 vector 값 ↔ .NET 유도값 대조(①↔③ 직접 대조).
$cppVectors = @{}
foreach ($m in [regex]::Matches($text, '(?m)^\s*vector (\S+) = ([0-9a-f-]{36})\s*$')) {
    $cppVectors[$m.Groups[1].Value] = $m.Groups[2].Value
}
if ($cppVectors.Count -lt $vectorCount) { $failures.Add("C++가 찍은 vector 줄이 $($cppVectors.Count)건 — 벡터 $vectorCount 건") }
foreach ($name in $dotnetUuids.Keys) {
    if ($cppVectors.ContainsKey($name) -and $cppVectors[$name] -ne $dotnetUuids[$name]) {
        $failures.Add("C++ ≠ .NET: $name $($cppVectors[$name]) vs $($dotnetUuids[$name])")
    }
}
$bcrypt = [regex]::Match($text, 'bcrypt agreement: (\d+)/(\d+)')
if (-not $bcrypt.Success -or $bcrypt.Groups[1].Value -ne $bcrypt.Groups[2].Value -or [int]$bcrypt.Groups[2].Value -lt 40) {
    $failures.Add('BCrypt 대조 줄이 없거나 불일치')
}

"asset-identity exit=$($process.ExitCode) output=$run"
"vectors=$vectorCount cliPass=$cliPass assertions=$assertions assertFailed=$assertFailed cppVectorLines=$($cppVectors.Count) bcrypt=$($bcrypt.Value)"
if ($process.ExitCode -ne 0) { $failures.Add("종료 코드 $($process.ExitCode)") }

if ($failures.Count -gt 0) { '실패:'; $failures | ForEach-Object { "  $_" }; exit 1 }
"전체 통과 — C++·Python·.NET 세 유도가 벡터 $vectorCount 건에서 같고 selftest 단정 $assertions 건 전부 통과"
exit 0
