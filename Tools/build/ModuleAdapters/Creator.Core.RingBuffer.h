#pragma once
// Generated from Engine/Contracts/Core/Creator.Core.RingBuffer.ixx; do not edit.
// Regenerate with: py Tools/build/generate-module-adapters.py
#if !defined(CE_CONTRACT_STANDALONE)
#error "Native consumers must import the named module directly."
#endif

#include "../../../Engine/Containers/Creator.Containers.h"
#include "../../../Engine/Utility_Framework/Foundation/StandardLibrary.h"

namespace ce
{
    // Retains the newest capacity items; snapshots are returned oldest first.
    // Capacity must be greater than zero, as required by the original container.
    template<typename T>
    class ring_buffer
    {
      public:
        explicit ring_buffer(std::size_t capacity) : capacity_(capacity) {}

        void push(const T& item)
        {
            if (buffer_.size() < capacity_)
            {
                buffer_.emplace_back(item);
            }
            else
            {
                buffer_[head_] = item;
                head_ = (head_ + 1) % capacity_;
            }
        }

        ce::dynamic_array<T> get_all() const
        {
            ce::dynamic_array<T> result;
            if (buffer_.size() < capacity_)
            {
                result = buffer_;
            }
            else
            {
                result.resize(capacity_);
                for (std::size_t i = 0; i < capacity_; ++i)
                {
                    const auto index = (head_ + i) % capacity_;
                    result[i] = buffer_[index];
                }
            }
            return result;
        }

        void clear()
        {
            cleared_ = true;
            buffer_.clear();
            head_ = 0;
        }

        bool is_clear() const
        {
            return cleared_;
        }

        void acknowledge_clear()
        {
            cleared_ = false;
        }

      private:
        ce::dynamic_array<T> buffer_;
        std::size_t head_{};
        std::size_t capacity_;
        bool cleared_{};
    };
}
