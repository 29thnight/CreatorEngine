# Creator.Containers

CreatorEngine가 소유하는 구현 정본은 Engine/Containers/modules의 22개 인터페이스 파티션과 Creator.Containers.ixx다. EngineContracts가 이를 직접 컴파일한다. STL 컨테이너에 의존하지 않으며 호환 헤더는 ../include/ce로 생성한다.

수정 후 이 디렉토리의 sync-headers.ps1을 실행한다. -Check는 생성물 일치를 검사한다. 한 바이너리에 헤더 API와 named module API를 혼합하지 않는다.

외부 CreatorContainers 솔루션은 테스트와 측정 드라이버만 소유한다. 그 루트 CMakeLists.txt에서 CE_IMPLEMENTATION_ROOT로 이 디렉토리의 부모를 지정하여 빌드한다. 외부 modules/validate-msvc.ps1 및 validate-linux.ps1은 엔진 정본을 검사한다. CMakeLists.txt의 테스트 경로는 외부 PROJECT_SOURCE_DIR을 기준으로 한다.

네이티브 엔진 모듈 그래프는 MSVC 19.51을 사용한다. 상세 성능 수용 결과와 실행 검증은 CreatorEngine/docs/analysis의 ModuleOptimizationDiagnostics20261026 및 ContainerEngineIntegration20261026을 참고한다.