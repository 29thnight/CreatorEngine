#include "PrsmBindingRegistry.h"
#include "PrsmBindingContext.h"

#include <reflgen/prsm/binding_abi.h>
#include <reflgen/prsm/validator.h>

#include <algorithm>

// 생성된 바인딩이 호출마다 부르는 단일 검증기(reflgen PrSM production ABI v1).
// 문맥·스레드를 먼저 보고, 컴파일된 네이티브 표에서 기호·다이제스트를 맞춘다.
extern "C" std::int32_t REFLGEN_PRSM_BINDING_CDECL ce_prsm_validate_binding(
    const std::uint8_t* symbol, std::uint32_t length, const std::uint8_t* digest) noexcept
{
    if (!prsm_engine::OnGameThread())
    {
        return REFLGEN_PRSM_BINDING_WRONG_THREAD;
    }
    if (nullptr == prsm_engine::CallScope::Current())
    {
        return REFLGEN_PRSM_BINDING_INVALID_CONTEXT;
    }
    return prsm_engine::BindingRegistry::Get().Validate(symbol, length, digest);
}

namespace prsm_engine
{
    BindingRegistry& BindingRegistry::Get()
    {
        static BindingRegistry registry = [] {
            BindingRegistry r;
            RegisterEngineBindingModules(r);
            return r;
        }();
        return registry;
    }

    void BindingRegistry::Register(BindingTableFn table)
    {
        if (nullptr != table && std::find(m_tables.begin(), m_tables.end(), table) == m_tables.end())
        {
            m_tables.push_back(table);
        }
    }

    std::vector<BindingSymbol> BindingRegistry::Symbols() const
    {
        std::vector<BindingSymbol> symbols;
        for (const BindingTableFn table : m_tables)
        {
            std::uint32_t count = 0;
            const reflgen_prsm_binding_entry* entries = table(&count);
            for (std::uint32_t i = 0; i < count; ++i)
            {
                symbols.push_back({ entries[i].symbol, reinterpret_cast<const void*>(entries[i].address) });
            }
        }
        return symbols;
    }

    std::int32_t BindingRegistry::Validate(
        const std::uint8_t* symbol, std::uint32_t length, const std::uint8_t* digest) const noexcept
    {
        // 표마다 한 번씩 본다. 같은 기호가 두 표에 있으면 validate_binding 이 ABI 불일치로 거부한다.
        reflgen::prsm::binding_table_view views[16]{};
        const std::uint32_t count = static_cast<std::uint32_t>(std::min<std::size_t>(m_tables.size(), std::size(views)));
        if (m_tables.size() > std::size(views))
        {
            return REFLGEN_PRSM_BINDING_ABI_MISMATCH;
        }
        for (std::uint32_t i = 0; i < count; ++i)
        {
            views[i].entries = m_tables[i](&views[i].count);
        }
        return reflgen::prsm::validate_binding(views, count, symbol, length, digest);
    }
} // namespace prsm_engine
