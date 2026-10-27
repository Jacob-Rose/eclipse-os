// Copyright 2024 | Jake Rose
//
// This file is part of project eclipse-os
// See readme.md for full license details.

#pragma once

#include <ecore/core.h>

#if !USE_ARDUINO

#include <functional>
#include <memory>
#include <vector>

#include "relic.h"
#include "strip_projection.h"

namespace eio
{
    /* @brief A relic with nothing behind it, for a host: one strip of nodes
    * that relic states render through, no screen, no buttons.
    *
    * The relic looks branch on which segment a node belongs to, so the id
    * claimed here decides which branch a rig gets. `makeNode` says what each
    * node is (where it sits, what it is part of); left empty, every node is a
    * plain mapped node at the origin.
    *
    *     HostRelicIO io;
    *     io.build(24, kSegmentId, [&](HSVStripSegment* segment, size_t idx) {
    *         auto node = std::make_shared<HSVStripNode_Mapped2D>(segment, static_cast<int>(idx));
    *         node->coord = coords[idx];
    *         return node;
    *     });
    */
    class HostRelicIO : public RelicIO
    {
    public:
        using NodeMaker = std::function<std::shared_ptr<HSVStripNode_Mapped2D>(HSVStripSegment* segment, size_t idx)>;

        void build(uint16_t count, uint8_t segmentId, const NodeMaker& makeNode = {});

        HSVStrip* getStrip() const { return strip.get(); }
        const std::vector<std::shared_ptr<HSVStripNode_Mapped2D>>& getNodes() const { return nodes; }

    private:
        std::unique_ptr<HSVStrip> strip;
        std::vector<std::shared_ptr<HSVStripNode_Mapped2D>> nodes;
    };
}

#endif
