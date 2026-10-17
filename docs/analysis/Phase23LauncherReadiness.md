# Launcher 착수 가능 범위와 미구현 조건

> 기준: master `493536e300e274cbf59989cdd62b20310c8ae64f`의 소스 정적 조사. 이 문서의 재작성에서 빌드·실행·성능 측정은 하지 않았다.

## 지금 재사용할 수 있는 경계

`open-project`는 배포 검증 → 설정별 프로젝트 pin 검증 → managed compile → Editor `--development-project` 실행으로 이어진다. `EnginePaths`는 host가 해석한 root를 받을 수 있다. 이 두 경계가 제품 descriptor adapter의 출발점이다.

그러나 Editor의 `--project`는 현재 오류를 발생시킨다. `*.creatorproject`를 정상 제품 입력으로 받아들이는 parser, Launcher 실행 프로젝트와 MSI 설치 흐름이 있다고 주장할 수 없다. 디렉터리를 인자로 받는 개발 adapter와 제품 프로젝트 identity는 서로 다른 계약이다.

## 착수 전에 닫을 결정

Descriptor schema와 projectId, exact version/buildId resolve, 설치 inventory의 서명 책임, installer의 side-by-side identity, 개발 배포 허용 정책을 결정해야 한다. path는 프로젝트 내부 root 경계를 지키고, 손상된 pin/manifest나 reparse 경로는 조용히 다른 엔진으로 fallback하지 않아야 한다.

첫 구현은 import→validate→open과 목록만 제거하는 동작에 집중한다. project create는 template를 후보에 만든 뒤 검증·발행하는 구조가 준비된 후 연결한다. Editor watcher는 session 수명 및 main-thread mutation queue와 함께 다중 root를 지원해야 하며 Launcher가 이를 대신 감시하지 않는다.

## 착수와 완료를 가르는 증거

현재 소스 조사로 개발 이음새가 확인된 것이지 설치 gate가 통과한 것은 아니다. clean Windows VM, 두 빌드 동시 설치·개별 repair/uninstall, 프로젝트 원본 digest 불변, descriptor 직접 실행·이동·중복 session 실패를 별도 검증해야 한다. 단계별 요구의 정본은 [EngineDistributionAndLauncherPlan](../plans/EngineDistributionAndLauncherPlan.md)이다.

## 구현 근거

- [BuildTool/Program.cs](../../BuildTool/Program.cs)
- [BuildTool/EngineDistribution.cs](../../BuildTool/EngineDistribution.cs)
- [Editor/EngineEntry/App.cpp](../../Editor/EngineEntry/App.cpp)
- [Engine/Utility_Framework/EnginePaths.h](../../Engine/Utility_Framework/EnginePaths.h)
