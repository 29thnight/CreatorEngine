// 실패해야 하는 항목 — import 만으로 TypeTrait.h 의 매크로를 쓴다.
//
// type_guid(T) 는 `#define type_guid(T) TypeTrait::GUIDCreator::GetTypeID<T>()`
// 다. 이름 있는 모듈은 매크로를 내보내지 않으므로 여기서 type_guid 는 없는
// 이름이어야 한다. 이 파일이 컴파일되면 모듈 경계가 매크로를 막지 못한다는
// 뜻이고, 그러면 "Windows/DX 층을 먼저 떼어 낸다" 는 전제부터 다시 봐야 한다.
//
// 판정 근거는 오류 문안에 type_guid 가 나오는 것이다(모듈을 못 찾아 실패한
// 것을 통과로 읽지 않는다).
import ce.probe.typetrait;

int main()
{
	const HashedGuid id = type_guid(int);
	return id == HashedGuid{} ? 1 : 0;
}
