// Copyright 2024 | Jake Rose
//
// This file is part of project eclipse-os
// See readme.md for full license details.

#pragma once

#include <string>
#include <vector>

#include <ecore/property.h>
#include <ecore/reflectable.h>

#include "universe.h"

namespace edmx
{
    /* @brief Maps incoming DMX channels onto a look's properties, so a
    * conventional lighting desk can turn its knobs.
    *
    * A channel's 0..255 lands on the named property scaled across its range;
    * a bool is on from 128 up. Apply it whenever a frame has arrived:
    *
    *     binding.bindChannel(1, "gain");
    *     binding.bindChannel(2, "inverted");
    *     ...
    *     binding.apply(input.channels(), look);
    */
    class ChannelBinding
    {
    public:
        struct Entry
        {
            int channel{1};        ///< 1-based, as the desk's patch sheet numbers it
            std::string property;
        };

        void bindChannel(int channel, const char* property);
        void clear();

        const std::vector<Entry>& entries() const { return bound; }

        /// Writes every bound channel's value into `target`. Returns how many landed.
        int apply(const ChannelBuffer& channels, ecore::Reflectable& target) const;

        /// The same, against a bag already built from the target.
        int apply(const ChannelBuffer& channels, ecore::PropertyBag& bag) const;

    private:
        std::vector<Entry> bound;
    };
}
