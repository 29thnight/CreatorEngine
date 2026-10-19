#pragma once
// reflgen 이 생성한 PrSM 바인딩 표(ce_prsm_table_<module>)의 모음.
//
// 엔진 클래스의 노출은 각 네이티브 모듈(SceneRuntime·RenderEngine …)의 header 에 단
// reflgen::interop("prsm") 이 정하고, 모듈마다 생성기가 표 하나와 카탈로그 하나를 낸다.
// 이 등록소는 그 표들을 모아 두 곳에 공급한다:
//   - JIT 허가 심볼(이름 → 주소). 스크립트 모듈은 여기 있는 래퍼만 부를 수 있다(D-047).
//   - ce_prsm_validate_binding 의 근거. 카탈로그 JSON 이 아니라 컴파일된 표가 권한이다.
// PrsmScriptHost 는 도메인(어느 엔진 클래스가 열려 있는지)을 모른다.
#include <cstdint>
#include <vector>

struct reflgen_prsm_binding_entry;

// 생성된 바인딩이 호출마다 부르는 단일 검증기. PrsmBindingRegistry.cpp 가 정의한다.
extern "C" std::int32_t ce_prsm_validate_binding(
    const std::uint8_t* symbol, std::uint32_t length, const std::uint8_t* digest) noexcept;

namespace prsm_engine
{
    using BindingTableFn = const reflgen_prsm_binding_entry* (*)(std::uint32_t* count) noexcept;

    struct BindingSymbol
    {
        const char* name;
        const void* address;
    };

    class BindingRegistry
    {
    public:
        static BindingRegistry& Get();

        // 엔진 기동 때 모듈 목록(PrsmBindingModules.cpp)이 한 번씩 부른다.
        void Register(BindingTableFn table);

        // 등록된 모든 표의 진입점(중복 기호는 검증기가 ABI 불일치로 거부한다).
        std::vector<BindingSymbol> Symbols() const;

        // reflgen::prsm::validate_binding 계약: 기호·다이제스트가 정확히 한 표 항목과 맞아야 한다.
        std::int32_t Validate(const std::uint8_t* symbol, std::uint32_t length, const std::uint8_t* digest) const noexcept;

        std::size_t TableCount() const { return m_tables.size(); }

    private:
        std::vector<BindingTableFn> m_tables;
    };

    // PrsmBindingModules.cpp 가 정의한다: 엔진에 링크된 바인딩 모듈의 표를 전부 등록한다.
    void RegisterEngineBindingModules(BindingRegistry& registry);
} // namespace prsm_engine
