// Copyright 2024 | Jake Rose 
//
// This file is part of project eclipse-os
// See readme.md for full license details.

#pragma once

#include <vector>
#include <memory>

#include "../ecore/hsv.h"
#include "../ecore/coord.h"

using namespace ecore;
using namespace std;

namespace eio
{
    ///
    /// 2d mapping of led strips used so states/patterns can query data as they see fit.
    ///


    enum class StripNodeType
    {
        ROOT,
        MAPPED1D,
        MAPPED2D,
    };

    class HSVStripNode
    {
    public:
        HSVStripNode();

        // since we dont support RTTI, we need to provide a way to check if a node is supported
        virtual StripNodeType GetStripNodeType() const { return StripNodeType::ROOT; }

    public:
        //led index
        int stripIdx; 
    };


    class HSVStripNode_Mapped2D : public HSVStripNode
    {
    public:
        HSVStripNode_Mapped2D() {}

        virtual StripNodeType GetStripNodeType() const override { return StripNodeType::MAPPED2D; }
    public:
        Coordinate coord;

    };

    class HSVStripNodeFactory
    {
    public:
        static vector<shared_ptr<HSVStripNode_Mapped2D>> GenerateAxisRow(float xDelta, float yDelta, int startIdx, int Length);
    };
}