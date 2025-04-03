// Copyright 2024 | Jake Rose 
//
// This file is part of project eclipse-os
// See readme.md for full license details.

#pragma once

#include <vector>
#include <memory>

#include "../ecore/core.h"

#include "../ecore/coord.h"
#include "../ecore/hsv.h"

#include "hsv_strip.h"

using namespace ecore; 

namespace eio
{
    ///
    /// 2d mapping of led strips used so states/patterns can query data as they see fit.
    ///
    class HSVStripNode_Mapped2D : public HSVStripNode
    {
    public:
        HSVStripNode_Mapped2D(HSVStripSegment* inParentStripSegment, int inStripIdx) : HSVStripNode(inParentStripSegment, inStripIdx) {}

        virtual StripNodeType GetStripNodeType() const override { return StripNodeType::MAPPED2D; }
    public:
        Coordinate coord;

    };

    class HSVStripNodeFactory
    {
    public:
        static std::vector<std::shared_ptr<HSVStripNode>> GenerateAxisRow(HSVStripSegment* strip_segment, int startIdx, int Length, const Coordinate& posStart, const Coordinate& posDeltaPerIdx);
    };
}