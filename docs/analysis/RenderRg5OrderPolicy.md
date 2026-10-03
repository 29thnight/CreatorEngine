# RG5-2 실행 순서 정책 분리 — 2026-10-03 검증 완료

2026-10-03 RGOrderPolicy를 선언/버전 모드와 별도 생성 인자로 추가했다. ExplicitVersioned의 Write/Modify·RAW/WAR/WAW·컬링 계약은 그대로 사용하며 DependencyOrder 또는 PreserveDeclarationOrder를 선택한다. 후자는 동일한 live DAG를 검증하고 살아 있는 역방향 의존성이 있으면 컴파일을 거부한다. legacy DeclarationOrder 제품 기본은 이번 묶음에서 전환하지 않는다.

검사는 동일한 버전 RAW 그래프의 정방향/역방향 선언 × 두 정책을 검사한다. DependencyOrder는 역방향 선언을 재정렬해야 하고 PreserveDeclarationOrder는 이를 거부해야 한다. 기존 GBuffer·Shadow 실제 Declare 검사와 함께 실행한다.

이는 제품 전체 소비자 이관의 선행 기반이다. 제품 소비자 Read/ReadWrite 이관·legacy 추론 제거·versioned 전체 프레임 GPU 수용·RG6 제품 기본 전환은 아직 남아 있다. RG5 전체 기성은 0이다. 이전 RG5-1 기준선은 변경 전 증거이며 이번 변경에 대한 현행 기준선 완료 판정은 아직 하지 않았다.
Debug/Release 빌드는 Build/rg5-order-Debug-build.log 및 Build/rg5-order-Release-build.log로 통과했다. 두 구성의 Build/Obj/RenderRG5Order/{Debug,Release}-native-v1/result.json에서 passed=true·exitCode=0을 확인했다. results.jsonl의 dx12.rendergraph와 quit은 모두 succeeded다. 정책 검사는 ValidateRg5ProducerDeclarations에서 반드시 호출하며 기존 RG5_PRODUCERS_OK 마커에 합류한다. 기존 RG1~4 GPU fixture는 두 구성 모두 픽셀 오차 0을 유지했다.

이번 검증은 native 계획·GPU fixture이며 제품 전체 장면 이미지 대조를 새로 수행하지 않았다. 이전 RG5-1의 phaseComplete는 변경 전 해시에 대한 증거로 보존한다. 현행 전체 프레임 기준선 갱신과 제품 GPU 수용은 후속 이관 게이트로 남는다. RG5-2 정책 분리 묶음은 완료지만 RG5 전체 완료는 아니다.

후속 RG5-3의 현행 기준선 갱신은 [소비 체인 기록](RenderRg5ConsumerMigration.md)을 따른다. 위 미갱신 표기는 RG5-2 완료 당시 상태다.
