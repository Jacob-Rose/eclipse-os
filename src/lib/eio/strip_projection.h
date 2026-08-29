// Copyright 2024 | Jake Rose
//
// This file is part of project eclipse-os
// See readme.md for full license details.

#pragma once

#include <vector>
#include <memory>
#include <string>

#include "../ecore/core.h"

#include "../ecore/coord.h"
#include "../ecore/hsv.h"

#include "hsv_strip.h"

using namespace ecore;

namespace eio
{
    ///
    /// Which physical thing a node is part of, when a rig is more than one.
    ///
    /// A stage is one coordinate space shared by everything on it - that is
    /// what lets a wave climb the ring and the obelisk as one wave. But a
    /// look that wants to treat the sculpture *as a sculpture* (a beam going
    /// round its four sides) or the truss as a truss (a scanner running its
    /// length) needs to know which nodes are which, and where each one is in
    /// its own object's terms. That is what a space is: the object, and the
    /// node's place in it, beside the stage coordinate it also has.
    ///
    /// `Stage` is the default and means "no object of its own" - a bare
    /// strip, a rig that never said. A look reads it as "use the stage".
    ///
    enum class NodeSpace : uint8_t
    {
        Stage,
        Ring,
        Obelisk,
        Truss,
    };

    /// The name a device file declares under "space", as a NodeSpace.
    /// Anything unrecognised is the stage, so an old file loses nothing.
    inline NodeSpace nodeSpaceFromName(const std::string& name)
    {
        if (name == "ring") return NodeSpace::Ring;
        if (name == "obelisk") return NodeSpace::Obelisk;
        if (name == "truss" || name == "pars") return NodeSpace::Truss;
        return NodeSpace::Stage;
    }

    ///
    /// 2d mapping of led strips used so states/patterns can query data as they see fit.
    ///
    class HSVStripNode_Mapped2D : public HSVStripNode
    {
    public:
        HSVStripNode_Mapped2D(HSVStripSegment* inParentStripSegment, int inStripIdx) : HSVStripNode(inParentStripSegment, inStripIdx) {}

        virtual StripNodeType GetStripNodeType() const override { return StripNodeType::MAPPED2D; }

        /// Non-null when this node also knows the space it belongs to.
        ///
        /// The house downcast without RTTI - the same idiom as
        /// Pattern::asStateMachine - rather than a fourth StripNodeType: a
        /// spaced node *is* a mapped node, and every look that checks for
        /// MAPPED2D and casts (the jacket's, the obelisk's) keeps working on
        /// one unchanged. Only a look that asks for the space sees it.
        virtual const class HSVStripNode_Space* asSpace() const { return nullptr; }
    public:
        Coordinate coord;

    };

    ///
    /// A mapped node that also knows which object it is part of, and where
    /// it sits in that object's own terms.
    ///
    /// `coord` (inherited) is still the stage coordinate every spatial look
    /// reads. What this adds is the *relative* view: the object's own
    /// coordinates before the environment placed it (`local` - for the
    /// obelisk that is strip 0..7 and height 0..42, whatever the stage did to
    /// it), the same normalized over the object's extent (`u`, `v`), and the
    /// node's place among the object's nodes in wiring order.
    ///
    class HSVStripNode_Space : public HSVStripNode_Mapped2D
    {
    public:
        using HSVStripNode_Mapped2D::HSVStripNode_Mapped2D;

        virtual const HSVStripNode_Space* asSpace() const override { return this; }

        NodeSpace space{NodeSpace::Stage};

        /// this node among its object's nodes, wiring order, and how many
        int index{0};
        int count{1};

        /// the object's own coordinates, before placement
        Coordinate local;

        /// `local` normalized over the object's extent, 0..1 on each axis;
        /// an axis with no extent (a flat row) reads 0.5
        float u{0.0f};
        float v{0.0f};
    };

    /// The space view of a node, or null when it has none - a bare strip, or
    /// a rig that never said what it was. Looks branch on this and fall back
    /// to the stage, so a look written for the objects still runs on a strip.
    inline const HSVStripNode_Space* spaceOf(const HSVStripNode* node)
    {
        if (node == nullptr || node->GetStripNodeType() != StripNodeType::MAPPED2D)
        {
            return nullptr;
        }
        return static_cast<const HSVStripNode_Mapped2D*>(node)->asSpace();
    }

    inline NodeSpace nodeSpace(const HSVStripNode* node)
    {
        const HSVStripNode_Space* spaced = spaceOf(node);
        return spaced ? spaced->space : NodeSpace::Stage;
    }

    class HSVStripNodeFactory
    {
    public:
        static std::vector<std::shared_ptr<HSVStripNode>> GenerateAxisRow(HSVStripSegment* strip_segment, int startIdx, int Length, const Coordinate& posStart, const Coordinate& posDeltaPerIdx);
    };
}
