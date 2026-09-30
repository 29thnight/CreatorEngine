# MAT-2 `.shadergraph` fixture

독립 예제의 Material 검사가 실제 저장·재개방으로 생성하고 검증한 schema 1 문서다.

- `constant.shadergraph`: HDR Color → Principled → Material Output, Float Blackboard,
  모든 기본 값 타입, frame·접힘·view.
- `nested-group.shadergraph`: 재사용 group과 중첩 group, 안정 interface,
  소켓의 비기본 색 공간·표시 상태.
- `image-normal.shadergraph`: Image Texture → Normal Map → Principled,
  typed reroute, Texture/Sampler 파라미터와 자산 참조.
- `unknown.shadergraph`: 미등록 vendor node와 extension payload.
  읽기 전용 저장 왕복은 가능하며 IR 생성은 지정 진단으로 실패해야 한다.

이 파일들은 GPU 렌더 golden이 아니다. 독립 예제의 현재 화면은 여전히 `.lxg`
조작 예제이며 `.shadergraph` UI adapter는 LX-3에서 연결한다.
형식과 API는 [MaterialGraphSchema.md](../../../../docs/design/MaterialGraphSchema.md)를 따른다.
