# Mathematics 의존성과 소비 규약

> 기준: master `493536e300e274cbf59989cdd62b20310c8ae64f`의 소스 정적 조사. 이 문서의 재작성에서 빌드·실행·성능 측정은 하지 않았다.

## 고정된 의존성

수학 구현은 ThirdParty/Mathematics/include의 header-only vendor다. provenance가 가리키는 고정 revision은 `1f43e080f180db1afbf6e18cb3849b758858a496`이며 MIT license를 동봉한다. configure 중 최신 branch를 가져오는 구조가 아니다. provenance에 기록된 과거 upstream 시험 결과는 이번 checkout의 재실행 결과로 승격하지 않는다.

Directory.Build.targets가 include 경로와 NOMINMAX를 제공한다. 소비자는 math의 vector·matrix·quaternion과 필요한 개별 헤더를 사용한다. Mathematics.Intersect.h는 Windows의 near/far macro를 잠시 해제하고 되돌려 충돌을 국소화한다. 옛 Core.Mathf 중심의 확장 계획은 현행 경계가 아니다.

## 교체에서 보존할 의미

자료형 이름보다 좌표계·행렬 순서·clip depth 의미가 중요하다. 렌더링과 visibility 입력은 실제 GPU 계약의 row-vector 및 zero-to-one Z 가정을 확인해야 한다. 똑같이 4×4인 행렬도 transpose, handedness, camera jitter가 다르면 호환되지 않는다. transform 저장과 shader 업로드의 변환 지점까지 함께 검토한다.

CPU API 교체가 GPU packed 구조의 크기·alignment 변경을 허용하지 않는다. binary artifact와 native/managed 경계는 각자의 명시적 포맷을 유지한다. easing/tween 같은 vendor 기능의 존재만으로 엔진의 모든 UI나 animation 경로가 그 API를 사용한다고 추론하지 않는다.

## 업데이트 의도

다음 vendor 갱신은 정확한 commit·license·header diff를 기록하고 기존 소비자의 수치·좌표계 계약을 다시 검토해야 한다. 정적 include 통일과 실행 parity 검증은 별도 gate다. 기존 계획의 단계별 완료율 대신 이 문서와 vendor provenance를 유지한다.

## 구현 근거

- [ThirdParty/Mathematics/PROVENANCE.md](../../ThirdParty/Mathematics/PROVENANCE.md)
- [Directory.Build.targets](../../Directory.Build.targets)
- [Engine/Utility_Framework/Mathematics.Intersect.h](../../Engine/Utility_Framework/Mathematics.Intersect.h)
- [Engine/RenderEngine/GpuGeometryVisibility.h](../../Engine/RenderEngine/GpuGeometryVisibility.h)
