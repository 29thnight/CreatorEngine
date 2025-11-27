# UIManager

**Header:** `ScriptBinder/UIManager.h`

**Inheritance:** `: public DLLCore::Singleton<UIManager>`

> 자동 생성된 문서입니다. 실제 사용 시 구현 파일을 함께 참고하세요.

## Public Methods

### Public Methods 역할
| 멤버 | 예상 역할 |
| --- | --- |
| `MakeCanvas` | make canvas 동작을 수행합니다. |
| `AddCanvas` | canvas을(를) 추가합니다. |
| `DeleteCanvas` | delete canvas 동작을 수행합니다. |
| `CheckInput` | check input 동작을 수행합니다. |
| `FindCanvasName` | canvas name을(를) 탐색합니다. |
| `FindCanvasIndex` | canvas index을(를) 탐색합니다. |
| `Update` | update을(를) 갱신합니다. |
| `SortCanvas` | sort canvas 동작을 수행합니다. |
| `RegisterImageComponent` | register image component 동작을 수행합니다. |
| `RegisterTextComponent` | register text component 동작을 수행합니다. |
| `RegisterSpriteSheetComponent` | register sprite sheet component 동작을 수행합니다. |
| `UnregisterImageComponent` | unregister image component 동작을 수행합니다. |
| `UnregisterTextComponent` | unregister text component 동작을 수행합니다. |
| `UnregisterSpriteSheetComponent` | unregister sprite sheet component 동작을 수행합니다. |
## Public Properties

### Public Properties 역할
| 멤버 | 예상 역할 |
| --- | --- |
| `Singleton<UIManager>` | singleton<uimanager> 상태를 보관합니다. |
| `m_clickEvent` | m click event 상태를 보관합니다. |
| `Images` | images 상태를 보관합니다. |
| `Texts` | texts 상태를 보관합니다. |
| `SpriteSheets` | sprite sheets 상태를 보관합니다. |
| `CurCanvas` | cur canvas 상태를 보관합니다. |
| `SelectUI` | select ui 상태를 보관합니다. |
| `needSort` | need sort 상태를 보관합니다. |
