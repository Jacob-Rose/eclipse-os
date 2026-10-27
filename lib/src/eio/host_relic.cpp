// Copyright 2024 | Jake Rose
//
// This file is part of project eclipse-os
// See readme.md for full license details.

#include "host_relic.h"

#if !USE_ARDUINO

using namespace eio;

void HostRelicIO::build(uint16_t count, uint8_t segmentId, const NodeMaker& makeNode)
{
    strip_segments.clear();
    strips.clear();
    nodes.clear();

    strip.reset(new HSVStrip(count, 0));
    auto segment = std::make_unique<HSVStripSegment>(strip.get(), segmentId);
    for (size_t idx = 0; idx < count; ++idx)
    {
        std::shared_ptr<HSVStripNode_Mapped2D> node = makeNode ? makeNode(segment.get(), idx) : nullptr;
        if (!node)
        {
            node = std::make_shared<HSVStripNode_Mapped2D>(segment.get(), static_cast<int>(idx));
        }
        nodes.push_back(node);
        segment->addNode(node);
    }
    strip_segments.emplace(segmentId, std::move(segment));

    // full brightness: whatever is downstream applies its own master and gamma
    setGlobalBrightness(EBrightness::MAX);
}

#endif
