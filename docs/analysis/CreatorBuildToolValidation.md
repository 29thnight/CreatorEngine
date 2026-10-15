# CreatorBuildTool 검증 표면과 재현 조건

> 기준: master `493536e300e274cbf59989cdd62b20310c8ae64f`의 소스 정적 조사. 이 문서의 재작성에서 빌드·실행·성능 측정은 하지 않았다.

## 무엇을 검증하는 도구인가

현재 진입점은 `BuildTool/Program.cs`의 여덟 command이다. C# 도구는 .NET 10 win-x64 apphost로 출력되고 `../../Runtime/DotNet`을 우선 탐색하도록 csproj의 apphost 생성 단계에 연결되어 있다. PowerShell wrapper의 성공과 실제 게임의 준비 완료는 별도 증거다.

배포 로드는 manifest hash/inventory, native metadata 일치를 검사한다. 게임 컴파일은 프로젝트 pin과 engine API를 확인한다. prebuilt GameScripts는 receipt와 DLL을 write/delete 공유 없이 열어 읽고, 원본·복사본·발행 전 bytes를 hash로 대조한다. 패키지는 Player smoke의 CEMF/cooked-document/parser-zero/presenter/managed-lifecycle marker 및 unpacked 파일 hash를 요구한다.

## 실패 결과를 읽는 법

`--skip-verify`는 성공 패키지가 아니라 unpublished candidate를 보존한다. packaging 도중 오류가 나면 current pointer는 보존되고 후보와 Player log 위치를 출력한다. Ctrl+C는 process tree 취소 경로와 exit 130으로 전달된다. timeout은 일반 build 오류다. BuildTool 자체가 종료되었다고 Player의 렌더링 정확성을 보증하지 않는다.

`BuildTool/Tests/Program.cs`는 source-level regression harness다. 테스트가 존재한다는 사실과 이 checkout에서 통과했다는 사실을 구분해야 한다. 이전 문서에 누적되었던 로컬 경로·실행 PID·성공 횟수는 현재 master 재검증 결과로 사용하지 않는다.

## 새 검증에 남길 자료

정확한 revision과 dirty 상태, engine buildId와 payloadDigest, Debug/Release 및 Shipping 여부, 입력 mode, 시작 scene, backend, command line, exit code, package/current manifest와 stdout/stderr를 함께 보존한다. smoke를 다시 돌리지 않았다면 해당 항목은 미실행으로 기록한다. clean VM, cancellation 중간 시점, Unicode/path adversarial fixture, source-free packaging은 각자 별도 수용축이다.

실행 절차와 데이터 흐름의 정본은 [BuildPipelinePlan](../plans/BuildPipelinePlan.md), source-free 계약은 [SourceFreeAssetSetPackaging](../design/SourceFreeAssetSetPackaging.md)이다.

## 구현 근거

- [BuildTool/Program.cs](../../BuildTool/Program.cs)
- [BuildTool/CreatorBuildTool.csproj](../../BuildTool/CreatorBuildTool.csproj)
- [BuildTool/Tests/Program.cs](../../BuildTool/Tests/Program.cs)
- [BuildTool/GameCompiler.cs](../../BuildTool/GameCompiler.cs)
- [BuildTool/PlayerVerification.cs](../../BuildTool/PlayerVerification.cs)
- [BuildTool/GamePackager.cs](../../BuildTool/GamePackager.cs)
