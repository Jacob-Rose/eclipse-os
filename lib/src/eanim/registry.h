// Copyright 2024 | Jake Rose
//
// This file is part of project eclipse-os
// See readme.md for full license details.

#pragma once

#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "generator_hsv.h"

namespace eanim
{
    /* @brief Looks by name. Content registers what it ships; a desk lists and
    * makes them, a relic picks one by name off a wire. The factory hands back
    * a look ready to tick: construct it and init it in there.
    *
    *     GeneratorRegistry& looks = GeneratorRegistry::shared();
    *     looks.add("scanner_boot", []() { return std::make_shared<Pattern_Scanner_Boot>(); });
    *     ...
    *     std::shared_ptr<GeneratorHSV> look = looks.make("scanner_boot");   // null if unknown
    *
    * @see registerRelicGenerators in src/relics/registry.h for everything the relics ship
    */
    class GeneratorRegistry
    {
    public:
        using Factory = std::function<std::shared_ptr<GeneratorHSV>()>;

        /// The process's registry.
        static GeneratorRegistry& shared();

        /// Adds or replaces a look. The last word on a name wins.
        void add(const std::string& name, Factory factory);
        bool has(const std::string& name) const;
        std::shared_ptr<GeneratorHSV> make(const std::string& name) const;

        /// Every name, sorted.
        std::vector<std::string> names() const;
        size_t size() const { return factories.size(); }
        void clear() { factories.clear(); }

    private:
        std::map<std::string, Factory> factories;
    };
}
