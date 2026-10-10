# CreatorEngine 기여 안내

버그 보고, 문서 수정과 코드 개선은 GitHub Issue와 Pull Request로 제안할 수 있습니다. 큰 설계 변경은 구현 전에 현재 소비 경로와 문제를 설명하는 Issue로 논의하십시오. 참여 시 [행동 규범](CODE_OF_CONDUCT.md)을 따릅니다.

## 시작하기

- [README](README.md): 프로젝트 소개와 빠른 시작
- [기술설명서](docs/TechnicalGuide.md): 구현 구조와 기능의 경계
- [문서 색인](docs/README.md): 분야별 설계·계획·검증 기록
- [코드 컨벤션](docs/design/CodingConventions.md): 이름·형식과 적용 기준

계획 문서는 목표와 남은 작업을 설명합니다. 현재 구현을 판단할 때는 master의 소스·호출 경로·프로젝트 편입을 먼저 확인하십시오. 계획의 완료 표시나 PR 병합만으로 새 변경의 실행 검증을 대체하지 않습니다.

## 버그와 기능 제안

[Issues](https://github.com/29thnight/CreatorEngine/issues)에서 유사한 보고를 확인한 뒤 새 Issue를 작성합니다.

버그 보고에는 재현 단계, 기대·실제 동작, 커밋 hash, Debug/Release·Shipping 여부, Windows·GPU·드라이버와 backend를 포함하십시오. 가능하면 최소 씬·입력과 오류 로그를 첨부합니다. 계정 정보, 개인 경로, 토큰과 비공개 자산은 제거합니다.

기능 제안에는 해결하려는 작업, 현재의 불편, 영향받는 소비자와 수용 조건을 적습니다. 성능 문제는 장면·입력·워밍업·표본과 측정 범위를 함께 제시합니다. FPS·CPU 시간·GPU 시간·진단 capture 시간을 서로 바꿔 쓰지 않습니다.

## 개발 환경

엔진 개발은 Windows x64에서 VS 18 계열·MSVC v145·Windows SDK·.NET 10 SDK·vcpkg·PowerShell 7을 사용합니다. Python 3는 정적 경계 검사에 필요합니다. 의존성과 첫 빌드 절차는 [README의 시작하기](README.md#시작하기)를 따릅니다.

fork 또는 허용된 저장소에서 master를 기준으로 주제별 브랜치를 만듭니다. 브랜치 이름은 작업 내용을 나타내며, 기존 작업 중인 파일은 보존합니다. 산출물·사용자 설정·캐시를 변경에 섞지 않습니다.

```powershell
git switch -c feat/describe-the-change
git status --short
```

## 변경 작성 기준

- 문제를 재현하고 현재 호출자·소유자·실패 경로를 확인한 뒤 변경합니다.
- 기능·리팩터링·대규모 포맷 변경은 검토 가능한 단위로 나눕니다. 관련 없는 파일은 수정하지 않습니다.
- 기존 API·자산 신원·저장 형식·마지막 정상 결과를 보존해야 하는 경계를 확인합니다. 변경이 필요하면 호환·이주·fallback 방법을 설명합니다.
- 새 수명·스레딩·GPU 제출 코드는 소유자, 게시 경계와 완료 조건을 명시합니다.
- 엔진 객체와 공용 STL형 유틸리티의 명명 규칙은 [CodingConventions](docs/design/CodingConventions.md)를 따릅니다. 포맷은 [.clang-format](.clang-format)을 기준으로 하며 namespace 내부는 들여씁니다. `if`·`for` 본문은 중괄호를 사용하는 여러 줄 형식으로 작성합니다.
- 새 리플렉션 필드의 저장 여부와 GC 대상의 trace 의무를 별도로 검토합니다. worker에서 편집 세션이나 Scene 객체를 임의로 갱신하지 않습니다.
- 의존성 변경은 vcpkg manifest 또는 ThirdParty의 출처·버전·hash·라이선스와 실제 소비자까지 갱신합니다.

## 검증

변경 영역에 필요한 검사를 선택합니다. 관련 없는 전체 회귀를 무조건 실행하지 않으며, 문서만 변경한 경우 엔진 전체 빌드를 요구하지 않습니다.

```powershell
# 공백 오류와 runtime/editor include 경계
git diff --check
python .\scripts\check_include_boundary.py

# 독립 BuildTool 계약 검사
dotnet run --project .\BuildTool\Tests\CreatorBuildTool.Tests.csproj -c Debug

# 실제 Editor 호스트 빌드: VS 설치 위치에 맞게 조정합니다.
$msbuild = 'C:\Program Files\Microsoft Visual Studio\18\Community\MSBuild\Current\Bin\amd64\MSBuild.exe'
& $msbuild .\Editor\CreatorEditor.vcxproj /t:Build /m /p:Configuration=Debug /p:Platform=x64
```

native 헤더·include를 바꿨다면 관련 프로젝트의 non-unity 빌드도 검토합니다. 런타임·씬 수명·Play/Stop 변경은 실제 호스트로 상태 전환·복원·실패와 Undo 보존을 확인합니다. 렌더 변경은 대상 backend·구성의 빌드와 실제 GPU 출력·동기화·수명을 확인합니다. 성능 비교는 GPU validation·진단 readback의 비용을 일반 프레임 표본과 구분합니다.

분야별 절차는 [회귀 검사](Tools/regression/README.md), [DX12 검증](Tools/dx12-validation/README.md), [프로파일링 검증](Tools/profiling-validation/README.md), [배포 가이드](Tools/distribution/README.md)에 있습니다. GPU나 장비가 없어 실행하지 못한 검사는 미실행으로 적고, 작성된 테스트를 통과한 테스트로 표시하지 않습니다.

문서 변경은 상대 링크·목차·코드 예제의 경로와 명령을 확인합니다. 구현 범위 변경을 설명한다면 관련 설계·정본 계획·대시보드의 내용도 필요에 따라 맞추되, 미수용 작업을 완료로 올리지 않습니다.

## Pull Request

PR은 처음 읽는 사람이 문제와 결과를 이해할 수 있도록 작성합니다.

1. 문제와 재현 조건, 변경 후 동작을 설명합니다.
2. 영향받는 API·저장 형식·소유권과 호환성 또는 fallback을 적습니다.
3. 수행한 정적 검사·빌드·런타임·GPU·성능 검증을 구분합니다. 커밋·환경·결과와 재현 명령을 포함합니다.
4. 미실행 검사, 남은 수용 조건과 알려진 한계를 적습니다.
5. 관련 Issue와 설계 문서를 연결하고, UI 변경에는 실제 화면을 첨부합니다.

변경 파일을 확인하고 명시한 경로만 stage합니다. 테스트 결과·로그·덤프·빌드 복사본은 근거를 요약하거나 적절한 첨부로 제공하고 대용량 산출물을 저장소에 넣지 않습니다. 공개 website workflow의 성공은 native 빌드·GPU 회귀 통과를 의미하지 않습니다.

관리자가 검토 후 반영 여부를 결정합니다. 응답 시간이나 모든 제안의 병합을 보장하지 않으며, 범위·설계·검증에 따라 수정이나 분리가 필요할 수 있습니다.

## AI 보조와 출처

AI 보조 도구를 사용할 수 있습니다. 제안·생성 결과도 직접 이해하고 소스·호출 경로·권한·라이선스와 검증 결과를 확인한 뒤 제출하십시오. AI 응답이나 자동 검사 한 종류만으로 품질을 보증하지 않습니다. 출처가 불분명하거나 이용 권한이 없는 코드·자산과 비공개 자료는 제출하지 않습니다.

## 이용 권한과 라이선스

현재 루트에는 프로젝트 전체 `LICENSE`가 없습니다. 이 안내는 별도 이용·재배포 라이선스나 CLA를 부여하지 않습니다. 프로젝트 이용 및 기여물의 권리 처리에 확인이 필요하면 구현·제출 전에 저장소 관리자와 Issue에서 논의하십시오. 외부 코드의 원래 출처·저작권 고지·라이선스를 유지합니다.
