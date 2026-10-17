# 엔진 배포와 Launcher 제품화 경계

> 기준: master `493536e300e274cbf59989cdd62b20310c8ae64f`의 소스 정적 조사. 이 문서의 재작성에서 빌드·실행·성능 측정은 하지 않았다.

## 구현된 출발점

현재 저장소는 불변 엔진 배포와 외부 개발 프로젝트 실행을 지원하는 코드를 갖는다. `publish-engine`이 검증된 host closure를 묶고, `select-engine`이 설정별 pin을 쓴다. `open-project`는 pin을 확인하고 관리 코드를 컴파일한 뒤 Editor에 `--development-project`와 `--managed-root`를 전달한다. 실제 배포 layout은 manifest의 `Bin/x64-<Configuration>` 및 그 아래 `Editor`, `Player`, `Tools`, `Runtime`이다.

Editor의 `MakeEditorLaunchConfig`는 `--project`가 주어지면 제품 descriptor 시작이 미구현이라는 오류를 낸다. 현재 `--development-project` adapter나 checkout의 Dynamic_CPP 탐색을 제품 Launcher로 설명해서는 안 된다. `EnginePaths`는 host가 project/content/data/assets root와 authoring capability를 주입할 수 있는 연결점이다.

## 유지할 제품 요구: 미구현 의도

목표 제품 흐름은 서명된 MSI → 설치된 exact engine → `*.creatorproject` → 해당 Editor 실행이다. 다음은 현재 제공 기능이 아니라 구현할 계약이다.

1. Descriptor가 projectId와 요구 엔진 version/buildId, 프로젝트 내부 authoring root를 소유한다. 최근 프로젝트 목록은 cache이며 identity의 정본이 아니다
2. Launcher는 설치 catalog를 검증하고 Create/Import/Open/목록 제거를 제공한다. 목록 제거는 프로젝트 파일 삭제와 분리한다
3. Editor host도 descriptor를 독립 검증한다. Launcher가 전달했다는 이유만으로 path/identity를 신뢰하지 않는다
4. ProjectSession이 동일 프로젝트의 쓰기 session lock, 실패 cleanup, authoring root 등록 수명을 소유한다
5. 엔진 payload는 설치 위치에서 읽기 전용이고, 프로젝트/생성물/log는 사용자 쓰기 위치에 둔다. repair/update/uninstall은 사용자 원본을 수정하지 않는다
6. Launcher는 asset watcher를 갖지 않는다. Editor의 efsw watcher가 다중 root, callback value queue와 missed-event rescan을 책임지도록 이관한다
7. Launcher 빌드 UI는 CreatorBuildTool을 별도 프로세스로 실행하고 진행률·취소·로그를 중계한다. 패키징 로직을 복제하지 않는다

## 수명과 신뢰 경계

설치 catalog가 directory name을 근거로 엔진을 등록하면 안 된다. `EngineDistribution.Load`의 hash/inventory 검증을 재사용하되, 제품 배포의 서명·다운로드 출처 검증은 별도로 설계해야 한다. 현재 manifest integrity는 서명된 공급망 검증과 동일하지 않다.

Editor는 한 프로세스에서 한 프로젝트의 authoring state를 소유하는 방향을 유지한다. multi-root watcher callback은 향후 main-thread mutation queue로 전달하고, root 제거/프로젝트 종료 때 watcher 수명이 확실히 끝나야 한다. Player에는 descriptor watcher, Launcher 상태, efsw를 넣지 않는다.

## 구현 순서와 수용 조건

- DL0: 정상·변조 descriptor, 틀린 buildId, 누락 파일, duplicate projectId, Unicode/긴 경로 fixture와 설치 topology를 먼저 고정
- DL1: descriptor parser와 Editor `--project`를 연결하고 development adapter와 명확히 구분
- DL2/DL3: 다중 root 등록·해제 및 missed-event rescan 수명 검증
- DL4: 최소 Launcher의 import/resolve/open과 index-only remove, 이후 template stage→검증→publish create 연결
- DL5/DL6: MSI identity, side-by-side 설치, repair/uninstall 보존, 서명과 clean VM 검증
- 후속: migration/rollback, build UI, 오류 복구와 업데이트 UX

수용 환경에는 repository, Visual Studio, vcpkg, 전역 .NET SDK가 없어야 한다. 서로 다른 exact build 동시 설치 및 개별 제거 후 프로젝트 digest 불변을 확인해야 한다. 네 자리 엔진 버전과 MSI identity의 매핑도 실제 설치 결과로 검증한다. 현재 소스 조사나 이전 개발 smoke로 이 조건을 대체하지 않는다.

현재 판정은 개발 배포 경로 구현, 제품 descriptor/Launcher/MSI 미구현이다. [준비도 분석](../analysis/Phase23LauncherReadiness.md)과 [버전 계약](../design/EngineVersionPolicy.md)을 함께 본다.

## 구현 근거

- [BuildTool/Program.cs](../../BuildTool/Program.cs)
- [BuildTool/EngineDistribution.cs](../../BuildTool/EngineDistribution.cs)
- [BuildTool/EnginePublisher.cs](../../BuildTool/EnginePublisher.cs)
- [Editor/EngineEntry/App.cpp](../../Editor/EngineEntry/App.cpp)
- [Engine/Utility_Framework/EnginePaths.h](../../Engine/Utility_Framework/EnginePaths.h)
- [Editor/EngineEntry/EditorAssetDatabase.cpp](../../Editor/EngineEntry/EditorAssetDatabase.cpp)
