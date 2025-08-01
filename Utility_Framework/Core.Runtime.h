#pragma once
#include <string>
#include <exception>

namespace std
{
	class null_exception : public std::exception
	{
	public:
		explicit null_exception(const std::string& message)
			: m_message(message)
		{
		}

		const char* what() const noexcept override
		{
			return m_message.c_str();
		}

	private:
		std::string m_message{ "null exception" };
	};
}