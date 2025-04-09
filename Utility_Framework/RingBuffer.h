// RingBuffer.h
#pragma once
#include <vector>
#include "Benchmark.hpp"
#include <iostream>
#include <ranges>
#include <algorithm>

template <typename T>
class RingBuffer {
public:
    explicit RingBuffer(size_t maxSize) : maxSize_(maxSize), head_(0) {}

    void push(const T& item) 
    {
        if (buffer_.size() < maxSize_) 
        {
            buffer_.emplace_back(item);
        }
        else 
        {
            buffer_[head_] = item;
            head_ = (head_ + 1) % maxSize_;
        }
    }

    std::vector<T> get_all()
    {
		Benchmark banch;
        size_t size = buffer_.size();
		std::vector<T> result(size);

        for (size_t i = 0; i < size; ++i) 
        {
            size_t idx = (head_ + i) % size;
            result[i] = buffer_[idx];
        }

		std::cout << "RingBuffer : " << banch.GetElapsedTime() << "\n";
        return result;
    }

	void clear()
	{
		isClear = true;

        buffer_.clear();

        head_ = 0;
	}

	bool IsClear() const
	{
		return isClear;
	}

	void toggleClear()
	{
		isClear = false;
	}

private:
    std::vector<T> buffer_;
    size_t head_;
    size_t maxSize_;
	bool isClear = false;
};
