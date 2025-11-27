# NodeFactory

**Header:** `ScriptBinder/NodeFactory.h`

**Inheritance:** `: public DLLCore::Singleton<NodeFactory>`

> 자동 생성된 문서입니다. 실제 사용 시 구현 파일을 함께 참고하세요.

## Public Methods
- `using CreateNodeFunc = std::function<BTNode::NodePtr()>;`
- `void Register(const std::string& typeName, CreateNodeFunc func);`
- `BTNode::NodePtr Create(const std::string& typeName);`

## Public Properties
- `std::map<std::string, CreateNodeFunc> m_registry;`
