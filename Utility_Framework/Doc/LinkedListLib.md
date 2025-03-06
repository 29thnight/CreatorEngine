# `LinkedList` 클래스 및 `LinkProperty` 구조체

## 개요

이 라이브러리는 **LinkedList**와 **LinkProperty**를 사용하여 연결된 리스트 자료구조를 구현합니다. 객체를 연결하여 관리하며, 이 리스트는 **LinkProperty**를 상속받은 객체를 담을 수 있습니다.
[https://github.com/29thnight/LinkedListLib]

## `LinkProperty` 구조체

`LinkProperty`는 리스트의 각 요소가 연결될 수 있도록 링크 정보를 제공합니다. `_pPrev`, `_pNext`, `_pElement`를 통해 각 요소를 연결합니다.

### 생성자

```cpp
LinkProperty(_Type* pType);
```
- `pType`: 리스트에 저장할 요소를 가리키는 포인터입니다.

### 메서드

- `InternalSeparate()`: 해당 링크의 이전, 다음 링크를 `nullptr`로 설정하여 연결을 끊습니다.

## `LinkedList` 클래스

`LinkedList`는 이중 연결 리스트를 구현한 클래스입니다. 링크를 추가하고 제거할 수 있으며, `Iterator`를 사용하여 순차적으로 접근할 수 있습니다.

### `Iterator` 클래스

리스트를 순회할 수 있는 **Iterator** 클래스를 제공합니다. 각 요소를 가리키고, `operator++`, `operator*`, `operator!=`를 통해 순차 접근이 가능합니다.

#### 생성자

```cpp
Iterator(LinkProperty<_Type>* pProperty);
```
- `pProperty`: 리스트의 첫 번째 링크를 가리키는 포인터입니다.

#### 연산자

- `operator++()`: 다음 요소로 이동합니다.
- `operator*()`: 현재 요소를 반환합니다.
- `operator!=(const Iterator& other)`: 두 `Iterator`가 동일한지 비교합니다.
- `operator->()`: 현재 요소를 반환합니다.

### 주요 메서드

#### `Link`

```cpp
void Link(LinkProperty<_Type>* pNewLinkProperty);
```
- `pNewLinkProperty`: 리스트에 추가할 새로운 링크입니다.
- 리스트 끝에 새 링크를 연결합니다.

#### `Unlink`

```cpp
void Unlink(LinkProperty<_Type>* pDeleteLinkProperty);
```
- `pDeleteLinkProperty`: 삭제할 링크입니다.
- 링크를 연결 리스트에서 제거하고 연결을 끊습니다.

#### `erase`

```cpp
void erase(Iterator& begin, Iterator& end);
```
- `begin`: 삭제를 시작할 위치의 `Iterator`.
- `end`: 삭제를 끝낼 위치의 `Iterator`.
- 주어진 범위에 있는 링크들을 삭제합니다.

#### `ClearLink`

```cpp
void ClearLink();
```
- 리스트에 있는 모든 링크를 삭제하고 연결을 끊습니다.

## 사용 예시

`LinkedList`와 `LinkProperty`를 사용하여 객체를 링크드 리스트에 연결할 수 있습니다. 객체는 `LinkProperty`를 상속받아야 합니다.

```cpp
struct MyObject : public LinkProperty<MyObject>
{
    int value;
    MyObject(int val) : value(val) {}
};

LinkedList<MyObject> list;
MyObject obj1(1), obj2(2), obj3(3);
list.Link(&obj1);
list.Link(&obj2);
list.Link(&obj3);

for (auto it = list.begin(); it != list.end(); ++it)
{
    std::cout << it->value << std::endl; // 1, 2, 3 출력
}
```

이 예시에서는 `MyObject`를 `LinkProperty`에서 상속받고, `LinkedList`에 링크하여 순차적으로 출력합니다.
```
