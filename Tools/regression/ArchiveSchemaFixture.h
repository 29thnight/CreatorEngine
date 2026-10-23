#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include <reflgen/core/schema.h>
struct ArchiveSchemaFixture
{
    std::uint32_t id{};
    std::string name;
    bool active{};
    std::vector<int> samples;
    static consteval auto reflect()
    {
        return reflgen::schema<ArchiveSchemaFixture>(
            reflgen::field<&ArchiveSchemaFixture::id>.named("id"),
            reflgen::field<&ArchiveSchemaFixture::name>.named("name"),
            reflgen::field<&ArchiveSchemaFixture::active>.named("active"),
            reflgen::field<&ArchiveSchemaFixture::samples>.named("samples"));
    }
};
