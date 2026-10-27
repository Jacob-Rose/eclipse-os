// Copyright 2024 | Jake Rose
//
// This file is part of project eclipse-os
// See readme.md for full license details.

#pragma once

#include <vector>
#include <memory>
#include <string>

#include <ecore/core.h>

#include <ecore/coord.h>
#include <ecore/hsv.h>

#include "hsv_strip.h"

using namespace ecore;

namespace eio
{
    /* @brief Which physical object a node is part of, when a rig has several.
    *
    * Every node has a stage coordinate; a space says which object it belongs
    * to as well, so a look can treat the obelisk as a sculpture or the truss
    * as a truss. Stage is the default and means "no object of its own".
    */
    enum class NodeSpace : uint8_t
    {
        Stage,
        Ring,
        Obelisk,
        Truss,
        /// Not a light: the point the visualiser reads the rig's colour from.
        /// A look with a palette paints it the palette's first colour.
        Probe,
    };

    /// The "space" name a device file declares, as a NodeSpace. Anything
    /// unrecognised is the stage.
    inline NodeSpace nodeSpaceFromName(const std::string& name)
    {
        if (name == "ring") return NodeSpace::Ring;
        if (name == "obelisk") return NodeSpace::Obelisk;
        if (name == "truss" || name == "pars") return NodeSpace::Truss;
        if (name == "probe") return NodeSpace::Probe;
        return NodeSpace::Stage;
    }

    /* @brief A node with a 2d stage coordinate, for looks that render by position. */
    class HSVStripNode_Mapped2D : public HSVStripNode
    {
    public:
        HSVStripNode_Mapped2D(HSVStripSegment* inParentStripSegment, int inStripIdx) : HSVStripNode(inParentStripSegment, inStripIdx) {}

        virtual StripNodeType GetStripNodeType() const override { return StripNodeType::MAPPED2D; }

        /// Non-null when this node also knows its space. The downcast without
        /// RTTI; prefer spaceOf(node).
        virtual const class HSVStripNode_Space* asSpace() const { return nullptr; }
    public:
        Coordinate coord;

    };

    /* @brief A mapped node that also knows which object it is part of and
    * where it sits in that object's own terms: `local` is the object's
    * coordinates before placement, `u`/`v` the same normalized 0..1, and
    * `index` the node's place among the object's nodes in wiring order.
    */
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

    /// The space view of a node, or null when it has none. Looks branch on
    /// this and fall back to the stage.
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
        /// `Length` mapped nodes from `startIdx`, placed from `posStart` by `posDeltaPerIdx` each.
        static std::vector<std::shared_ptr<HSVStripNode>> GenerateAxisRow(HSVStripSegment* strip_segment, int startIdx, int Length, const Coordinate& posStart, const Coordinate& posDeltaPerIdx);
    };
}
