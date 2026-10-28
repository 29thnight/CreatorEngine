`ce/*.h`는 `../modules/*.ixx` 정본으로부터 생성하는 호환 헤더다. 구현은 `.ixx`에서 수정하고 `../modules/sync-headers.ps1`로 재생성한다. named module 소비자는 `import Creator.Containers;`를 사용한다. 동일 프로그램에서 두 API 경로를 혼합하지 않는다.
