#pragma once

#include "LayerTypes.h"
#include <algorithm>
#include <array>
#include <atomic>
#include <limits>
#include <memory>
#include <optional>
#include <ranges>
#include <span>
#include <string>
#include <string_view>
#include <thread>

namespace ce::layers
{
struct layer_definition
{
    layer_id id;
    layer_slot slot;
    std::string name;
    bool retired = false;
};

struct catalog_snapshot
{
    std::uint64_t revision = 1;
    std::uint64_t next_id = 2;
    std::array<std::optional<layer_definition>, 32> definitions{};

    const layer_definition* Find(layer_id id) const noexcept
    {
        for (const auto& item : definitions)
            if (item && !item->retired && item->id == id)
                return &*item;

        return nullptr;
    }

    const layer_definition* Find(std::string_view name) const noexcept
    {
        for (const auto& item : definitions)
            if (item && !item->retired && item->name == name)
                return &*item;

        return nullptr;
    }

    std::uint32_t ActiveMask() const noexcept
    {
        std::uint32_t mask = 0;
        for (const auto& item : definitions)
            if (item && !item->retired)
                mask |= item->slot.Mask();

        return mask;
    }
};

// Bootstrap owns the catalog. Only its owner mutates; readers retain immutable snapshots.
class LayerCatalog final
{
  public:
    LayerCatalog()
    {
        catalog_snapshot initial;
        initial.definitions[0] = layer_definition{default_layer, {}, "Default"};
        m_snapshot.store(std::make_shared<const catalog_snapshot>(std::move(initial)));
    }

    std::shared_ptr<const catalog_snapshot> Snapshot() const noexcept { return m_snapshot.load(); }

    static result<void> Validate(const catalog_snapshot& value) noexcept
    {
        if (!value.revision || value.next_id < 2 || !value.definitions[0] ||
            value.definitions[0]->id != default_layer || value.definitions[0]->retired ||
            value.definitions[0]->name != "Default")
            return std::unexpected(error::invalid_definition);

        for (std::size_t i = 0; i < value.definitions.size(); ++i)
        {
            const auto& item = value.definitions[i];
            if (!item)
                continue;

            if (!item->id.value || item->id.value >= value.next_id || item->slot.Value() != i ||
                (item->name.size() > max_layer_name_bytes || !ValidLayerName(item->name)))
                return std::unexpected(error::invalid_definition);

            for (std::size_t j = 0; j < i; ++j)
                if (const auto& prior = value.definitions[j];
                    prior &&
                    (prior->id == item->id || (!prior->retired && !item->retired && prior->name == item->name)))
                    return std::unexpected(error::duplicate);
        }

        return {};
    }

    // Load/restore receives a complete validated definition. Revision is local and monotonic.
    result<void> Replace(catalog_snapshot value)
    {
        return Edit([&](catalog_snapshot& candidate) -> result<void> {
            const auto revision = candidate.revision;
            const auto next = candidate.next_id;
            candidate = std::move(value);
            candidate.revision = revision;
            candidate.next_id = (std::max)(next, candidate.next_id);
            // Restore may remove Play-created layers. Keep their slots retired, never free.
            const auto current = Snapshot();
            for (std::size_t slot = 0; slot < current->definitions.size(); ++slot)
            {
                const auto& previous = current->definitions[slot];
                if (!previous)
                    continue;

                auto& incoming = candidate.definitions[slot];
                if (incoming && incoming->id != previous->id)
                    return std::unexpected(error::invalid_definition);

                if (!incoming)
                {
                    incoming = previous;
                    incoming->retired = true;
                }
            }

            return Validate(candidate);
        });
    }

    result<layer_id> Add(std::string_view name)
    {
        layer_id created;
        auto edited = Edit([&](catalog_snapshot& value) -> result<void> {
            if (name.size() > max_layer_name_bytes || !ValidLayerName(name))
                return std::unexpected(error::invalid_definition);

            if (value.Find(name))
                return std::unexpected(error::duplicate);

            const auto empty = std::ranges::find_if(value.definitions, [](const auto& item) { return !item; });
            if (empty == value.definitions.end() || value.next_id == (std::numeric_limits<std::uint64_t>::max)())
                return std::unexpected(error::capacity_exceeded);

            created = layer_id{value.next_id++};
            const auto slot = layer_slot::Make(static_cast<std::uint32_t>(empty - value.definitions.begin())).value();
            *empty = layer_definition{created, slot, std::string(name)};
            return {};
        });

        if (!edited)
            return std::unexpected(edited.error());

        return created;
    }

    result<void> Rename(layer_id id, std::string_view name)
    {
        return Edit([&](catalog_snapshot& value) -> result<void> {
            if (id == default_layer)
                return std::unexpected(error::reserved_layer);

            if (name.size() > max_layer_name_bytes || !ValidLayerName(name))
                return std::unexpected(error::invalid_definition);

            const auto* item = value.Find(id);
            if (!item)
                return std::unexpected(error::unknown_layer);

            if (const auto* duplicate = value.Find(name); duplicate && duplicate->id != id)
                return std::unexpected(error::duplicate);

            value.definitions[item->slot.Value()]->name = name;
            return {};
        });
    }

    result<void> Retire(layer_id id)
    {
        return Edit([&](catalog_snapshot& value) -> result<void> {
            if (id == default_layer)
                return std::unexpected(error::reserved_layer);

            const auto* item = value.Find(id);
            if (!item)
                return std::unexpected(error::unknown_layer);

            value.definitions[item->slot.Value()]->retired = true;
            return {};
        });
    }

    // Migration only; preserving source positions also preserves every old mask bit.
    static result<catalog_snapshot> ImportLegacy(std::span<const std::string> names)
    {
        if (names.empty() || names.front() != "Default" || names.size() > 32)
            return std::unexpected(error::invalid_definition);

        try
        {
            catalog_snapshot value;
            value.next_id = names.size() + 1;
            for (const auto [index, name] : names | std::views::enumerate)
                value.definitions[index] =
                    layer_definition{layer_id{static_cast<std::uint64_t>(index + 1)},
                                     layer_slot::Make(static_cast<std::uint32_t>(index)).value(), name};

            if (auto valid = Validate(value); !valid)
                return std::unexpected(valid.error());

            return value;
        }
        catch (const std::bad_alloc&)
        {
            return std::unexpected(error::out_of_memory);
        }
    }

  private:
    template<class EditFunction>
    result<void> Edit(EditFunction&& edit)
    {
        if (m_owner != std::this_thread::get_id())
            return std::unexpected(error::wrong_owner);

        try
        {
            auto candidate = *Snapshot();
            if (candidate.revision == (std::numeric_limits<std::uint64_t>::max)())
                return std::unexpected(error::capacity_exceeded);

            if (auto edited = edit(candidate); !edited)
                return edited;

            ++candidate.revision;
            m_snapshot.store(std::make_shared<const catalog_snapshot>(std::move(candidate)));
            return {};
        }
        catch (const std::bad_alloc&)
        {
            return std::unexpected(error::out_of_memory);
        }
    }

    std::thread::id m_owner = std::this_thread::get_id();
    std::atomic<std::shared_ptr<const catalog_snapshot>> m_snapshot;
};
} // namespace ce::layers
