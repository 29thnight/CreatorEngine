#pragma once
//#include "Core.Memory.hpp"
#include <vector>
#include "CSVLoaderHelper.h"
#include "TypeDefinition.h"

class CSVReader
{
public:
    using Row = CSVRowView;

    explicit CSVReader(const std::string& filename, bool skipHeader = true)
    {
        std::ifstream file(filename);
        if (!file.is_open())
        {
            throw std::runtime_error("Could not open file: " + filename);
        }

        std::string line;
        if (!std::getline(file, line))
        {
            throw std::runtime_error("File is empty or cannot read the first line");
        }

        if (skipHeader)
        {
            parseHeader(line);
        }
        else
        {
            parseRow(line);
        }

        while (std::getline(file, line))
        {
            parseRow(line);
        }

        file.close();
    }

    auto begin() const noexcept { return m_viewData.begin(); }
    auto end() const noexcept { return m_viewData.end(); }

private:
    std::unordered_map<std::string, std::size_t> m_headerMap;
    std::deque<std::vector<std::string>> m_rawData;
    std::vector<CSVRowView> m_viewData;

    void parseHeader(const std::string& headerLine)
    {
        std::istringstream ss(headerLine);
        std::string cell;
        std::size_t index = 0;

        while (std::getline(ss, cell, ','))
        {
            m_headerMap[cell] = index++;
        }
    }

    void parseRow(const std::string& line)
    {
        std::vector<std::string> row;
        std::istringstream ss(line);
        std::string cell;

        while (std::getline(ss, cell, ','))
        {
            row.emplace_back(std::move(cell));
        }

        m_rawData.emplace_back(std::move(row));
        m_viewData.emplace_back(m_rawData.back(), m_headerMap);
    }
};