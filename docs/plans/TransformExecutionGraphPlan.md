# (통합됨) Sparse Compiled Transform Execution Graph 제안

**이 문서의 내용은 2026-08-25에 [`TransformUpdatePlan.md`](TransformUpdatePlan.md)로
통합됐다.** 여기서 유지하지 않는다.

`TransformUpdatePlan.md`가 PHASE 8.75의 단일 정본이며, 이 제안이 담고 있던 것은
다음 자리로 갔다.

| 원래 절 | 통합 후 |
|---|---|
| §0 판정 요약 | §0 판정 요약 — 권장 구조와 복잡도 표 |
| §1 측정·구조 전제 교정 | **§3** 측정·구조 전제 교정 (§3.1~§3.7). 소스 확인 표시와 §3.8·§3.9 신규 실측 추가 |
| §2 목표와 비목표 | §5 |
| §3 목표 소유 구조 | §6 (다이어그램 재작성) |
| §4 쓰기 계약 | §7 |
| §5 resolve 알고리즘 | §8 (프레임 플로우·SyncDerivedState·sparse resolve 다이어그램 신설) |
| §6 render publication | §9 |
| §7 데이터 표현 | §10 |
| §8 계층 mutation 계약 | §11 |
| §9 실행 슬라이스 X0~X9 | §12 (의존 다이어그램 신설, X0에 topology 변경 빈도 추가) |
| §10 성능·회귀 매트릭스 | §13 (스폰/파괴 시나리오 추가) |
| §11 위험과 롤백 경계 | §14 |
| §12 기존 계획과의 관계 | **부록 A** — 폐기된 T0~T5와 판정 |
| §13 최종 권고 | §12 슬라이스 의존 다이어그램이 대체 |

통합 과정에서 더해진 것:

- **§3.8 X1 비용 실측** — `Transform` 한정 TRS 직접 쓰기 **12곳, 전부 에디터**
  (`InspectorWindow.cpp` 9 · `ImGuiDrawHelperRectTransformComponent.cpp` 3).
  느슨한 상한 104건은 대부분 다른 타입이다. X1은 대공사가 아니다.
- **§3.9 topology 변경 빈도 (미측정, X0-④ 신설)** — §8.5가 허용하는 "topology 변경 시
  O(N) 전체 compile"의 재개 조건이 *reparent*로만 잡혀 있었는데, **엔티티 생성·파괴도
  topology 변경**이다. 매 프레임 스폰하는 씬이면 O(N)/frame이 되어 지금보다 나빠질 수
  있다. X0에서 재고, 크면 incremental compile이 X9 선택지가 아니라 X4의 전제가 된다.
- **§6.3 소유자 명시** — WorldSpace Canvas처럼 두 projection에 모두 들어가는 Entity의
  `worldMatrix` 소유자는 Spatial graph 하나다.
- **§8.4 동등 비교의 한계 명시** — `nextWorld != worldMatrix[i]`는 64B float 완전 일치라
  FP 지터에 무력하고, range 순회 자체를 줄이지 않고 write·publish만 줄인다.
- **X5 완료 기준 단서** — skeleton 시나리오는 X7(bulk upload) 이후에 다시 잰다.
  X5 시점의 나쁜 수치를 설계 실패로 오독하지 않기 위해서다.
