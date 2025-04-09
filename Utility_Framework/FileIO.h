#pragma once
#include <filesystem>
#include <cstdio>

// FileReader 클래스는 IReader 인터페이스를 구현하여 파일 읽기 기능을 제공
class FileReader 
{
public:
	explicit FileReader(const std::filesystem::path& path) : filePath(path), file(nullptr) 
	{
		if (!std::filesystem::exists(filePath)) 
		{
			std::string throwMessage = "File does not exist: " + filePath.string();
			throw std::runtime_error(throwMessage);
		}

		errno_t err = fopen_s(&file, filePath.string().c_str(), "rb");
		if (err != 0)
		{
			std::string throwMessage = "Failed to open file for writing: " + filePath.string();
			throw std::runtime_error(throwMessage);
		}
	}

	~FileReader() 
	{
		if (file) 
		{
			std::fclose(file);
		}
	}

	std::size_t getFileSize() const 
	{
		return std::filesystem::file_size(filePath);
	}

	std::size_t read(char* buffer, std::size_t size) 
	{
		if (!file) 
		{
			throw std::runtime_error("File not opened for reading");
		}
		return std::fread(buffer, 1, size, file);
	}

private:
	std::filesystem::path filePath;
	std::FILE* file;
};

class FileWriter 
{
public:
	explicit FileWriter(const std::filesystem::path& path) : filePath(path), file(nullptr) 
	{
		errno_t err = fopen_s(&file, filePath.string().c_str(), "wb");
		if (err != 0)
		{
			std::string throwMessage = "Failed to open file for writing: " + filePath.string();
			throw std::runtime_error(throwMessage);
		}
	}

	~FileWriter() 
	{
		if (file) 
		{
			std::fclose(file);
		}
	}

	void write(const char* buffer, std::size_t size) 
	{
		if (!file) 
		{
			throw std::runtime_error("File not opened for writing");
		}
		if (std::fwrite(buffer, 1, size, file) != size) 
		{
			throw std::runtime_error("Failed to write to file: " + filePath.string());
		}
	}

	void flush() {
		if (file && std::fflush(file) != 0) 
		{
			throw std::runtime_error("Failed to flush file: " + filePath.string());
		}
	}

private:
	std::filesystem::path filePath;
	std::FILE* file;
};
