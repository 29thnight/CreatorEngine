// 리플렉션 컨테이너 값 왕복 계약 (range 일반화 · 런타임 축)
//
// ── 분류 게이트가 못 보는 것 ────────────────────────────────────────────
//
// `verify-reflection-container.ps1` 은 **형상 판정**을 잡는다: 무엇이 시퀀스이고
// 무엇이 맵이며 어떤 수단으로 채우는가. 그 판정이 옳아도 값이 돌아온다는 보장은
// 없다 — 키를 적는 표기와 읽는 표기가 어긋나거나, 빈 컨테이너 표기가 한쪽
// 방향에서만 널로 읽히거나, 고정 크기 배열의 꼬리가 갱신되지 않을 수 있다.
// 그 축은 **값을 실제로 저장하고 다시 읽어야만** 보인다.
//
// ── 무엇을 재는가 ──────────────────────────────────────────────────────
//
//   ① 값 왕복 — 컨테이너 종류별로 채워 저장하고, 새 객체로 읽어 원본과 맞댄다.
//   ② 형상   — 방출된 YAML 텍스트를 직접 본다. 값이 왕복해도 형상이 틀릴 수
//              있고(맵을 시퀀스로 적어도 왕복은 성립한다), 형상은 디스크 호환의
//              정본이다. 특히 **std::string 이 글자 시퀀스로 새지 않았는가**.
//   ③ 빈 것  — 빈 시퀀스와 빈 맵이 **같은 표기**(`~`)로 나가는가.
//
// ── 어느 바이너리를 재는가 ─────────────────────────────────────────────
//
// 직렬화기는 전부 템플릿이라 **이 TU 에서 새로 컴파일된다.** 재는 축은 항상
// 현재 소스다. 링크되는 Utility_Framework.lib 는 로거·레지스트리·ryml 경계 같은
// 비템플릿 곁다리만 공급한다 — 그쪽이 낡아도 판정 축은 낡지 않는다.

#include "ReflectionTypedYml.h"
#include "AuthoringParsedDocument.h"

#include <array>
#include <cstdio>
#include <cstring>
#include <deque>
#include <list>
#include <map>
#include <set>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace
{
    enum class ProbeKind { Alpha = 0, Beta = 1, Gamma = 2 };

    struct ProbeNested
    {
        static consteval auto reflect()
        {
            using Self = ProbeNested;
            return reflgen::schema<Self>(
                reflgen::field<&Self::m_id>,
                reflgen::field<&Self::m_tag>);
        }

        int m_id{ 0 };
        std::string m_tag{};

        bool operator==(const ProbeNested&) const = default;
    };

    struct ProbeOwnedRecord
    {
        ProbeOwnedRecord() = default;
        explicit ProbeOwnedRecord(int initial) : value(initial) {}
        static consteval auto reflect()
        {
            using Self = ProbeOwnedRecord;
            return reflgen::schema<Self>(reflgen::field<&Self::value>,
                reflgen::field<&Self::beforeWrites>);
        }
        void OnBeforeSerialize() { ++beforeWrites; }
        int value{};
        int beforeWrites{};
    };

    struct ProbeRecursiveValue
    {
        static consteval auto reflect()
        {
            using Self = ProbeRecursiveValue;
            return reflgen::schema<Self>(reflgen::field<&Self::children>);
        }
        std::vector<ProbeRecursiveValue> children;
    };
    static_assert(Meta::Typed::CanSnapshotImmutable<ProbeRecursiveValue>());

    struct ProbeExclusiveNested
    {
        static consteval auto reflect()
        {
            using Self = ProbeExclusiveNested;
            return reflgen::schema<Self>(reflgen::field<&Self::items>);
        }
        std::vector<own::unique_owner<ProbeOwnedRecord>> items;
    };

    static_assert(!Meta::Typed::CanSnapshotImmutable<ProbeExclusiveNested>());
    static_assert(!Meta::Typed::CanSnapshotImmutable<
        std::vector<ProbeExclusiveNested>>());
    static_assert(!Meta::Typed::CanSnapshotImmutable<
        std::map<int, ProbeExclusiveNested>>());
    static_assert(Meta::Typed::CanSnapshotImmutable<ProbeOwnedRecord>());

    struct ProbeOwners
    {
        static consteval auto reflect()
        {
            using Self = ProbeOwners;
            return reflgen::schema<Self>(reflgen::field<&Self::shared>,
                reflgen::field<&Self::frozen>, reflgen::field<&Self::unique>,
                reflgen::field<&Self::frozenUnique>);
        }
        own::shared_owner<ProbeOwnedRecord> shared;
        own::shared_owner<const ProbeOwnedRecord> frozen;
        own::unique_owner<ProbeOwnedRecord> unique;
        own::unique_owner<const ProbeOwnedRecord> frozenUnique;
    };

    static_assert(Meta::Typed::PointerLike<own::shared_owner<ProbeOwnedRecord>>);
    static_assert(Meta::Typed::PointerLike<own::unique_owner<ProbeOwnedRecord>>);
    static_assert(!Meta::Typed::PointerLike<own::weak_owner<ProbeOwnedRecord>>);
    static_assert(!IsCopyableForProperty<std::vector<own::unique_owner<ProbeOwnedRecord>>>());
    static_assert(std::is_same_v<Meta::Typed::PointeeT<own::shared_owner<const ProbeOwnedRecord>>,
        ProbeOwnedRecord>);
    static_assert(std::is_same_v<decltype(Meta::Typed::RawPtrOf(
        std::declval<own::shared_owner<const ProbeOwnedRecord>&>())), const ProbeOwnedRecord*>);

    struct ProbeFixture
    {
        static consteval auto reflect()
        {
            using Self = ProbeFixture;
            return reflgen::schema<Self>(
                reflgen::field<&Self::m_name>,
                reflgen::field<&Self::m_path>,
                reflgen::field<&Self::m_vectorInt>,
                reflgen::field<&Self::m_vectorString>,
                reflgen::field<&Self::m_vectorBool>,
                reflgen::field<&Self::m_vectorEnum>,
                reflgen::field<&Self::m_vectorRect>,
                reflgen::field<&Self::m_vectorColor>,
                reflgen::field<&Self::m_vectorNested>,
                reflgen::field<&Self::m_deque>,
                reflgen::field<&Self::m_list>,
                reflgen::field<&Self::m_set>,
                reflgen::field<&Self::m_unorderedSet>,
                reflgen::field<&Self::m_array>,
                reflgen::field<&Self::m_mapStringInt>,
                reflgen::field<&Self::m_mapIntVector3>,
                reflgen::field<&Self::m_mapEnumValue>,
                reflgen::field<&Self::m_mapNested>,
                reflgen::field<&Self::m_unorderedMap>,
                reflgen::field<&Self::m_emptyVector>,
                reflgen::field<&Self::m_emptyMap>);
        }

        // ★ 음성 축. 이 둘이 시퀀스로 새면 range 일반화가 무너진 것이다.
        std::string m_name{};
        file::path m_path{};

        std::vector<int> m_vectorInt{};
        std::vector<std::string> m_vectorString{};
        std::vector<bool> m_vectorBool{};        // 프록시 원소
        std::vector<ProbeKind> m_vectorEnum{};   // 레거시에 갈래가 없던 자리
        std::vector<math::rect> m_vectorRect{};  // 레거시에 갈래가 없던 자리
        std::vector<math::color> m_vectorColor{};
        std::vector<ProbeNested> m_vectorNested{};

        std::deque<float> m_deque{};
        std::list<int> m_list{};
        std::set<int> m_set{};                          // const 원소
        std::unordered_set<std::string> m_unorderedSet{};
        std::array<float, 4> m_array{};                 // 고정 크기

        std::map<std::string, int> m_mapStringInt{};
        std::map<int, math::vector3> m_mapIntVector3{};
        std::map<std::string, ProbeKind> m_mapEnumValue{};
        std::map<std::string, ProbeNested> m_mapNested{};
        std::unordered_map<std::string, std::string> m_unorderedMap{};

        std::vector<int> m_emptyVector{};
        std::map<std::string, int> m_emptyMap{};
    };

    ProbeFixture MakeAuthored()
    {
        ProbeFixture f;
        // 이름에 컨테이너로 오해될 만한 것을 일부러 넣지 않는다 — 평범한 문자열이
        // 새는지를 보는 것이 요점이다.
        f.m_name = "authored name";
        f.m_path = file::path("Assets/Probe/thing.asset");

        f.m_vectorInt = { 3, -1, 42 };
        f.m_vectorString = { "alpha", "beta gamma", "" };
        f.m_vectorBool = { true, false, true, true };
        f.m_vectorEnum = { ProbeKind::Gamma, ProbeKind::Alpha };
        f.m_vectorRect = { math::rect{ 1.f, 2.f, 3.f, 4.f },
                           math::rect{ 5.f, 6.f, 7.f, 8.f } };
        f.m_vectorColor = { math::color{ 0.25f, 0.5f, 0.75f, 1.f } };
        f.m_vectorNested = { ProbeNested{ 7, "seven" }, ProbeNested{ 9, "nine" } };

        f.m_deque = { 1.5f, -2.5f };
        f.m_list = { 10, 20, 30 };
        f.m_set = { 5, 1, 9 };
        f.m_unorderedSet = { "one", "two" };
        f.m_array = { 0.5f, 1.5f, 2.5f, 3.5f };

        f.m_mapStringInt = { { "first", 1 }, { "second", 2 } };
        f.m_mapIntVector3 = { { 4, math::vector3{ 1.f, 2.f, 3.f } } };
        f.m_mapEnumValue = { { "kind", ProbeKind::Beta } };
        f.m_mapNested = { { "entry", ProbeNested{ 3, "three" } } };
        f.m_unorderedMap = { { "key", "value" } };
        return f;
    }

    int g_axes = 0;
    int g_failures = 0;

    void Check(const char* axis, bool ok)
    {
        ++g_axes;
        if (!ok) { ++g_failures; }
        std::printf("axis %-20s %s\n", axis, ok ? "ok" : "MISMATCH");
    }

    // Source-only ownership/constness cases; this probe was not executed as
    // part of the AssetDepot implementation session.
    void CheckOwnerRoundtrip()
    {
        const auto noncopyable = own::make_shared<const ProbeExclusiveNested>();
        Authoring::WriteDocument rejectedDocument;
        Authoring::ReflgenWriter rejectedWriter(rejectedDocument.Root());
        bool noncopyableRejected = false;
        try
        {
            reflgen::serialize(rejectedWriter, noncopyable);
        }
        catch (const reflgen::serialization_error&)
        {
            noncopyableRejected = true;
        }
        Check("owner-nested-exclusive-rejected", noncopyableRejected);

        ProbeOwners source;
        source.shared = own::make_shared<ProbeOwnedRecord>(11);
        source.frozen = own::make_shared<const ProbeOwnedRecord>(22);
        source.unique = own::make_unique<ProbeOwnedRecord>(33);
        source.frozenUnique = own::make_unique<const ProbeOwnedRecord>(44);
        Authoring::WriteDocument document;
        Meta::Typed::SerializeObjectInto(source, document.Root());
        Check("owner-mutable-hook", source.shared->beforeWrites == 1 && source.unique->beforeWrites == 1);
        Check("owner-const-not-mutated", source.frozen->beforeWrites == 0
            && source.frozenUnique->beforeWrites == 0);
        std::string failure;
        const auto parsed = Authoring::ParsedDocument::ParseText(document.Dump(), failure);
        Check("owner-output-parses", static_cast<bool>(parsed));
        if (!parsed)
        {
            return;
        }
        ProbeOwners loaded;
        Meta::Typed::DeserializeObjectFrom(loaded, parsed.Root());
        Check("owner-shared-roundtrip", loaded.shared && loaded.shared->value == 11);
        Check("owner-const-roundtrip", loaded.frozen && loaded.frozen->value == 22);
        Check("owner-unique-roundtrip", loaded.unique && loaded.unique->value == 33);
        Check("owner-const-unique-roundtrip", loaded.frozenUnique && loaded.frozenUnique->value == 44);
        auto previous = loaded.shared;
        const auto invalid = Authoring::ParsedDocument::ParseText("shared: {value: not-an-int}\n", failure);
        bool rejected = false;
        try
        {
            Meta::Typed::DeserializeObjectFrom(loaded, invalid.Root());
        }
        catch (const std::exception&)
        {
            rejected = true;
        }
        Check("owner-failed-read-transactional", rejected && loaded.shared && previous
            && &*loaded.shared.borrow() == &*previous.borrow() && loaded.shared->value == 11);
    }

    // 텍스트에 정확히 이 줄이 있는가. 형상 단정은 줄 단위로 건다 — 부분 문자열
    // 매치는 다른 필드의 값에 우연히 걸린다(이 저장소가 이미 겪은 오판 양식).
    bool HasLine(const std::string& text, const std::string& needle)
    {
        std::size_t begin = 0;
        while (begin <= text.size())
        {
            const std::size_t end = text.find('\n', begin);
            std::string line = text.substr(begin,
                (std::string::npos == end) ? std::string::npos : end - begin);
            while (!line.empty() && ('\r' == line.back() || ' ' == line.back()))
            {
                line.pop_back();
            }
            std::size_t lead = 0;
            while (lead < line.size() && ' ' == line[lead]) { ++lead; }
            if (line.substr(lead) == needle) { return true; }
            if (std::string::npos == end) { break; }
            begin = end + 1;
        }
        return false;
    }
}

int main(int argc, char** argv)
{
    // `--dump` 은 방출 텍스트를 그대로 낸다. 형상 단정을 새로 걸거나 고칠 때
    // 눈으로 검산하기 위한 것이다 — 단정이 무엇을 재는지 모르는 채 통과시키는
    // 것이 골든류 검사가 조용히 눈머는 가장 흔한 경로다.
    bool dump = false;
    for (int i = 1; i < argc; ++i)
    {
        if (0 == std::strcmp(argv[i], "--dump")) { dump = true; }
    }

    CheckOwnerRoundtrip();

    const ProbeFixture authored = MakeAuthored();

    Authoring::WriteDocument document;
    {
        ProbeFixture source = authored;
        Meta::Typed::SerializeObjectInto(source, document.Root());
    }
    const std::string emitted = document.Dump();
    if (dump) { std::printf("%s\n", emitted.c_str()); }

    std::string parseError;
    const Authoring::ParsedDocument parsed =
        Authoring::ParsedDocument::ParseText(emitted, parseError);
    if (!parsed)
    {
        std::printf("parse failed: %s\n", parseError.c_str());
        std::printf("---- emitted ----\n%s\n", emitted.c_str());
        return 2;
    }

    ProbeFixture loaded;
    Meta::Typed::DeserializeObjectFrom(loaded, parsed.Root());

    // ── ① 값 왕복 ────────────────────────────────────────────────────────
    Check("string", loaded.m_name == authored.m_name);
    Check("path", loaded.m_path == authored.m_path);
    Check("vector-int", loaded.m_vectorInt == authored.m_vectorInt);
    Check("vector-string", loaded.m_vectorString == authored.m_vectorString);
    Check("vector-bool", loaded.m_vectorBool == authored.m_vectorBool);
    Check("vector-enum", loaded.m_vectorEnum == authored.m_vectorEnum);
    Check("vector-nested", loaded.m_vectorNested == authored.m_vectorNested);
    Check("deque", loaded.m_deque == authored.m_deque);
    Check("list", loaded.m_list == authored.m_list);
    Check("set", loaded.m_set == authored.m_set);
    Check("unordered-set", loaded.m_unorderedSet == authored.m_unorderedSet);
    Check("array", loaded.m_array == authored.m_array);
    Check("map-string-int", loaded.m_mapStringInt == authored.m_mapStringInt);
    Check("map-enum-value", loaded.m_mapEnumValue == authored.m_mapEnumValue);
    Check("map-nested", loaded.m_mapNested == authored.m_mapNested);
    Check("unordered-map", loaded.m_unorderedMap == authored.m_unorderedMap);
    Check("empty-vector", loaded.m_emptyVector.empty());
    Check("empty-map", loaded.m_emptyMap.empty());

    {
        bool ok = loaded.m_vectorRect.size() == authored.m_vectorRect.size();
        for (std::size_t i = 0; ok && i < authored.m_vectorRect.size(); ++i)
        {
            const auto& a = authored.m_vectorRect[i];
            const auto& b = loaded.m_vectorRect[i];
            ok = (a.x == b.x && a.y == b.y && a.width == b.width && a.height == b.height);
        }
        Check("vector-rect", ok);
    }
    {
        bool ok = loaded.m_vectorColor.size() == authored.m_vectorColor.size();
        for (std::size_t i = 0; ok && i < authored.m_vectorColor.size(); ++i)
        {
            const auto& a = authored.m_vectorColor[i];
            const auto& b = loaded.m_vectorColor[i];
            ok = (a.r == b.r && a.g == b.g && a.b == b.b && a.a == b.a);
        }
        Check("vector-color", ok);
    }
    {
        bool ok = loaded.m_mapIntVector3.size() == authored.m_mapIntVector3.size();
        for (const auto& [key, expected] : authored.m_mapIntVector3)
        {
            const auto found = loaded.m_mapIntVector3.find(key);
            if (found == loaded.m_mapIntVector3.end()) { ok = false; break; }
            ok = ok && found->second.x == expected.x
                && found->second.y == expected.y
                && found->second.z == expected.z;
        }
        Check("map-int-vector3", ok);
    }

    // ── ② 형상 ───────────────────────────────────────────────────────────
    //
    // 값이 왕복해도 형상은 틀릴 수 있다. 맵을 {키, 값} 쌍의 시퀀스로 적어도
    // 왕복은 성립하지만 디스크 호환은 갈린다.
    Check("shape-string-scalar", HasLine(emitted, "m_name: authored name"));
    Check("shape-path-scalar",
        HasLine(emitted, "m_path: Assets/Probe/thing.asset"));
    // 스칼라 원소 시퀀스는 flow — 레거시 vecEntry 관례를 그대로 잇는다.
    Check("shape-scalar-seq-flow", HasLine(emitted, "m_vectorInt: [3,-1,42]")
        || HasLine(emitted, "m_vectorInt: [3, -1, 42]"));
    // 맵은 블록 맵이고 키가 그대로 키 자리에 선다.
    Check("shape-map-is-map", HasLine(emitted, "m_mapStringInt:")
        && HasLine(emitted, "first: 1") && HasLine(emitted, "second: 2"));
    Check("shape-map-int-key", HasLine(emitted, "m_mapIntVector3:")
        && HasLine(emitted, "4: {x: 1, y: 2, z: 3}"));

    // ── ③ 빈 것은 시퀀스도 맵도 같은 표기 ────────────────────────────────
    Check("shape-empty-seq-null", HasLine(emitted, "m_emptyVector: ~"));
    Check("shape-empty-map-null", HasLine(emitted, "m_emptyMap: ~"));

    std::printf("[REFLECTION ROUNDTRIP] axes=%d failures=%d\n", g_axes, g_failures);
    if (0 != g_failures)
    {
        std::printf("---- emitted ----\n%s\n", emitted.c_str());
        return 1;
    }
    return 0;
}
