// Copyright 2024 | Jake Rose
//
// This file is part of project eclipse-os
// See readme.md for full license details.

#include "registry.h"

using namespace eanim;

GeneratorRegistry& GeneratorRegistry::shared()
{
    static GeneratorRegistry instance;
    return instance;
}

void GeneratorRegistry::add(const std::string& name, Factory factory)
{
    factories[name] = std::move(factory);
}

bool GeneratorRegistry::has(const std::string& name) const
{
    return factories.find(name) != factories.end();
}

std::shared_ptr<GeneratorHSV> GeneratorRegistry::make(const std::string& name) const
{
    const auto found = factories.find(name);
    if (found == factories.end() || !found->second)
    {
        return nullptr;
    }
    return found->second();
}

std::vector<std::string> GeneratorRegistry::names() const
{
    std::vector<std::string> out;
    out.reserve(factories.size());
    for (const auto& entry : factories)
    {
        out.push_back(entry.first);
    }
    return out;
}
