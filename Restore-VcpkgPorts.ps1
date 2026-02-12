<#
.SYNOPSIS
  실행 경로의 vcpkg-installed-ports.json을 읽어
  동일 경로에 vcpkg-response.txt / vcpkg-install-log.txt를 생성하고
  클래식 모드로 일괄 설치합니다.
#>

param(
    [string]$VcpkgExe = "vcpkg.exe"   # PATH에 있거나 실행 경로에 있으면 그대로 사용
)

# 실행한 위치(현재 작업 디렉터리)
$CWD = Get-Location

# 파일 경로들: 전부 "실행한 위치" 기준
$JsonFile     = Join-Path $CWD 'vcpkg-installed-ports.json'
$ResponseFile = Join-Path $CWD 'vcpkg-response.txt'
$LogFile      = Join-Path $CWD 'vcpkg-install-log.txt'

Write-Host "📂 Working Dir : $CWD"
Write-Host "📄 JSON        : $JsonFile"
Write-Host "📝 Response    : $ResponseFile"
Write-Host "🧾 Log         : $LogFile"
Write-Host ""

if (-not (Test-Path $JsonFile)) {
    Write-Error "❌ JSON 파일이 실행 경로에 없습니다: $JsonFile"
    exit 1
}

try {
    $json = Get-Content $JsonFile -Raw | ConvertFrom-Json
} catch {
    Write-Error "❌ JSON 파싱 실패: $($_.Exception.Message)"
    exit 1
}

if (-not $json) {
    Write-Error "❌ JSON 내용이 비어 있거나 형식이 올바르지 않습니다."
    exit 1
}

Write-Host "📦 포트 목록 추출 중..."
$ports = @()

# JSON의 각 항목에서 package_name + triplet을 조합해 'pkg:triplet' 생성
foreach ($key in $json.PSObject.Properties.Name) {
    $entry   = $json.$key
    $pkg     = $entry.package_name
    $triplet = $entry.triplet
    if ($pkg -and $triplet) {
        $ports += ('{0}:{1}' -f $pkg, $triplet)  # 콜론은 포맷 문자열로 안전하게
    }
}

if ($ports.Count -eq 0) {
    Write-Error "❌ 추출된 포트가 없습니다."
    exit 1
}

# 중복 제거 + 정렬 후 반응파일로 저장 (실행 경로에 생성)
$ports = $ports | Sort-Object -Unique
$ports | Out-File -FilePath $ResponseFile -Encoding utf8

Write-Host "✅ 설치 대상 (${($ports.Count)})이 $ResponseFile 에 저장되었습니다."
$ports | ForEach-Object { Write-Host "  - $_" }

Write-Host "`n⚙️  vcpkg install @$(Split-Path -Leaf $ResponseFile) 실행 중..."
# vcpkg 실행 (로그를 실행 경로의 로그 파일로 저장)
& $VcpkgExe install "@$ResponseFile" 2>&1 | Tee-Object -FilePath $LogFile

Write-Host "`n✅ 완료. 로그: $LogFile"
