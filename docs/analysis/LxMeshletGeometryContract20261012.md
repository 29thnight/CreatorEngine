# LX 메시렛 쿠킹·GPU 계약 통일 — 2026-10-12

일반 GBuffer 제거 후 남아 있던 쿠킹 메시렛과 LX 런타임 재클러스터링의 중복을 제거한다. 신규 LX 배선만 사용하며 이전 산출물의 변환·indexed 복구·A/B 선택 경로를 제공하지 않는다.

## 식별자와 데이터 소유

| 데이터 | 계약 |
|---|---|
| geometry | 자산/세대에 연결된 밀봉 geometryKey |
| placement | objectId, incarnation, instanceId 각각 64-bit |
| material | 밀봉 LX materialSlot 64-bit; 향후 바인드리스 인덱스 구현과는 별개 |
| LOD | 해당 geometry의 sourceLodIndex; 그림자는 현재 LOD0 정책 유지 |
| cluster | 쿠커가 부여한 LOD 내 clusterId. 청크 분할·업로드에서 유지 |
| primitive | 해당 LOD의 최종 indexed 삼각형 순번. 파일 원본 polygon 순번이 아님 |
| vertex | 청크 내 변환 정점 인덱스와 원본 geometry 정점 인덱스를 함께 유지 |

64-byte `SceneGeometryIdentity`를 카메라·그림자 상수에 함께 기록한다. geometry/LOD와 cluster/primitive ID를 조합하면 원본 쿠킹 topology에 도달하고, placement와 materialSlot은 프레임의 밀봉된 인스턴스·재질을 식별한다.

쿠커는 최종 삼각형·corner 순서를 보존하는 단일 partition을 생성한다. LX는 이를 다시 클러스터링하지 않는다. 청크 경계는 쿠킹된 클러스터 경계에 맞추고, 정점은 청크의 변환 출력에 맞게 재배치하되 원본 ID를 함께 보존한다. 이것은 현재 계약의 GPU 직렬화이며 이전 산출물의 호환 변환이 아니다.

## 강제 재생성 경계

| 계약 | 현재 버전 | 이전 산출물 |
|---|---:|---|
| 모델 CEMC | 13 | 강제 리임포트·재쿠킹 |
| granular geometry artifact | 2 | 읽기 거부 |
| model descriptor | 5 | 읽기 거부 |
| meshlet profile / builder | 2 / 2 | 검증·GPU packing 거부 |
| GPU meshlet header | schema 2, 80B | 신규 카메라·그림자 소비자만 사용 |
| LX Scene host | 22 | 재질 Scene 제품 재생성·재쿠킹 |

meshlet/LOD 손상 시 indexed 또는 LOD0로 복구하던 codec 처리를 제거했다. 재임포트는 메시렛 생성을 강제하며, indexed-only 임포트 요청은 거부한다. 기존 원본 sidecar의 선택 플래그는 구 배선을 살리는 데 쓰지 않고 재임포트 때 현재 필수 정책으로 기록한다.

## GPU 배선

`PackLxMeshletGpuPayload`가 유일한 GPU 직렬화 계약이다. header는 descriptor, vertex remap, packed uint8 triangle corners, primitive IDs, original vertex IDs와 source LOD/extent를 가리킨다. DX12/Vulkan 모델 업로드와 LX 청크 업로드가 같은 계약을 사용한다.

`LxMeshletGpuContract.slang`을 카메라 MS·그림자 MS·그림자 compute cull이 공유한다. descriptor의 clusterId와 primitive remap은 쿠킹 정체성을 유지하며, compute 생존 목록의 값은 해당 GPU slice 내 descriptor 위치다. 이 위치를 자산의 전역 cluster ID로 오인하지 않는다.

양자화·클러스터 스트리밍·GPU LOD 선택·가시성 버퍼 셰이딩은 후속 작업이다. 이번 변경은 그 기능들이 공유할 데이터와 ID 계약을 확정한다.

## 검증

Debug·Release 최종 빌드와 다음 검증을 모두 통과했다. 검증 결과는 `Build/Verification/LxMeshletContract-20261012`에 기록했다.

| 구성 | CPU 계약 | GPU 생존 목록 | GPU 업로드 바이트 | 실제 표시 |
|---|---:|---:|---:|---|
| Debug-Accepted | 72/72 | 12건, 필수 누락 0 | 12건, schema/profile 2, 바이트 일치 | frame 686, 깊이/HDR 252/252 픽셀 |
| Release-Accepted | 72/72 | 144건, 필수 누락 0 | 144건, schema/profile 2, 바이트 일치 | frame 695, 깊이/HDR 252/252 픽셀 |

CPU 검사는 primitive/corner 순서, 청크 경계에서 cluster/primitive/original vertex ID 보존, geometry/instance/material/LOD scalar ABI, geometry codec 왕복, 이전 geometry 버전·누락·잘못된 ID 거부를 포함한다. GPU 검사는 실제 제출 뒤 topology buffer를 리드백하여 현재 canonical packing과 비교하며, 생존 ID의 범위·중복·필수 누락도 별도로 검사한다.

두 구성에서 LX 스키닝·rendergraph, 모델 강제 리임포트/로드/배치, `LX.Scene.GBuffer` mesh shader indirect 기록과 비어 있지 않은 유한 픽셀, GPU validation 문제/메시지 유실 0, 정상 종료 0을 확인했다. 실행 전후 소스와 바이너리 신원도 일치했다. 정적 제거 검사, 변경 범위 whitespace 검사와 문서 구조/링크 검사도 통과했다.

중간 실패의 로그·판정·검토 기록은 보존했다. 구 테스트의 잘못된 bounds/정점 배치와 쿠킹 topology 없는 RG5 픽스처를 수정했으며, indexed 복구를 기대하던 codec probe는 거부 검사로 바꿨다. 독립 codec probe 전체 시나리오를 실행했다고 주장하지 않는다. A:B는 사용자 몫이고 Vulkan 실행 검증은 PHASE 4.9에 남는다. 이번 결과는 성능 이득이나 RE 전체 파이프라인 완성의 수용이 아니다.
