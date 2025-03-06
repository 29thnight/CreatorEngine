#pragma once
#include <memory>
#include <mutex>

class Noncopyable
{
protected:
	Noncopyable() = default;
	~Noncopyable() = default;

protected:
	Noncopyable(const Noncopyable&) = delete;
	Noncopyable(Noncopyable&&) = delete;
	Noncopyable& operator=(const Noncopyable&) = delete;
	Noncopyable& operator=(Noncopyable&&) = delete;
};

template <typename T>
class Singleton : public Noncopyable
{
protected:
	Singleton() = default;
	virtual ~Singleton() = default;

public:
	inline static const std::shared_ptr<T>& GetInstance()
	{
		std::call_once(s_onceFlag, []()
		{
			T* prim = new T();

			s_instance = std::shared_ptr<T>(prim, Deleter());
		});

		return s_instance;

	}

	struct Deleter
	{
		void operator()(T* instance)
		{
			delete instance;
		}
	};

private:
	static std::shared_ptr<T> s_instance;
	static std::once_flag s_onceFlag;
};

template <typename T>
std::shared_ptr<T> Singleton<T>::s_instance = std::shared_ptr<T>();

template <typename T>
std::once_flag Singleton<T>::s_onceFlag;