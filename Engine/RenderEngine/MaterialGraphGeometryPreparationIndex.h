#pragma once

#include "../Utility_Framework/Foundation/StandardLibrary.h"

namespace material_graph
{
    // Declaration-local borrowed identities. Result indices are dense in first-use
    // order; the sparse hash table is scratch storage, never a result/owner table.
    // The caller keeps every input alive until all indexed results are prepared.
    class GeometryPreparationIndex final
    {
      public:
        struct Entry
        {
            const void* source{};
            std::size_t index{};
        };
        struct Result
        {
            std::size_t index{};
            bool inserted{};
        };

        static std::size_t StorageSize(std::size_t maximum)
        {
            if (maximum > (std::numeric_limits<std::size_t>::max)() / 4)
            {
                throw std::bad_array_new_length();
            }
            return std::bit_ceil((std::max)(std::size_t{8}, maximum * 2));
        }

        GeometryPreparationIndex(std::span<Entry> storage, std::size_t maximum) : storage_(storage), maximum_(maximum)
        {
            if (storage.size() != StorageSize(maximum))
            {
                throw std::invalid_argument("Geometry preparation index storage size mismatch.");
            }
            std::fill(storage_.begin(), storage_.end(), Entry{});
        }

        Result FindOrAdd(const void* source)
        {
            if (!source)
            {
                throw std::invalid_argument("Geometry preparation index requires a live input.");
            }
            auto hash = reinterpret_cast<std::uintptr_t>(source) >> 3;
            hash ^= hash >> 17;
            hash *= std::uintptr_t{0x9e3779b1};
            auto slot = hash & (storage_.size() - 1);
            while (storage_[slot].source)
            {
                if (storage_[slot].source == source)
                {
                    return {storage_[slot].index, false};
                }
                slot = (slot + 1) & (storage_.size() - 1);
            }
            if (size_ == maximum_)
            {
                throw std::length_error("Geometry preparation index exceeds its declared input count.");
            }
            const auto index = size_++;
            storage_[slot] = {source, index};
            return {index, true};
        }

        std::size_t Size() const { return size_; }

      private:
        std::span<Entry> storage_;
        std::size_t maximum_{};
        std::size_t size_{};
    };
} // namespace material_graph
