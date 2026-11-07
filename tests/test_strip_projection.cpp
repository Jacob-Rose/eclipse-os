// Copyright 2024 | Jake Rose
//
// This file is part of project eclipse-os
// See readme.md for full license details.

#include "eclipse_test.h"

#include <eio/strip_projection.h>

using namespace eio;

namespace
{
    const HSVStripNode_Mapped2D* mapped(const std::shared_ptr<HSVStripNode>& node)
    {
        return static_cast<const HSVStripNode_Mapped2D*>(node.get());
    }
}

TEST(strip_table_scales_to_one_unit_a_pixel)
{
    // a row in cm, 2.5 cm apart, that turns a corner after pixel 2
    const float coords[][2] = { {0.0f, 0.0f}, {2.5f, 0.0f}, {5.0f, 0.0f}, {5.0f, 2.5f}, {5.0f, 5.0f} };
    HSVStripSegment segment(nullptr, 0);
    const auto nodes = HSVStripNodeFactory::GenerateFromTable(&segment, 10, coords, 5);

    CHECK_EQ(static_cast<int>(nodes.size()), 5);
    CHECK_EQ(static_cast<int>(segment.getNodes().size()), 5);
    CHECK_EQ(nodes[0]->getStripIdx(), 10);
    CHECK_EQ(nodes[4]->getStripIdx(), 14);
    CHECK(nodes[0]->GetStripNodeType() == StripNodeType::MAPPED2D);
    CHECK_NEAR(mapped(nodes[1])->coord.x, 1.0f, 1e-5f);
    CHECK_NEAR(mapped(nodes[4])->coord.x, 2.0f, 1e-5f);
    CHECK_NEAR(mapped(nodes[4])->coord.y, 2.0f, 1e-5f);
}

TEST(strip_table_spacing_ignores_a_jump_between_rows)
{
    // two rows of three, one unit apart, with a long jump back between them
    const float coords[][2] = { {0, 0}, {1, 0}, {2, 0}, {0, 10}, {1, 10}, {2, 10} };
    CHECK_NEAR(HSVStripNodeFactory::MedianSpacing(coords, 6), 1.0f, 1e-5f);
}

TEST(strip_table_zero_spacing_places_as_written)
{
    const float coords[][2] = { {0.0f, 0.0f}, {40.0f, 30.0f} };
    HSVStripSegment segment(nullptr, 0);
    const auto nodes = HSVStripNodeFactory::GenerateFromTable(&segment, 0, coords, 2, 0.0f);
    CHECK_NEAR(mapped(nodes[1])->coord.x, 40.0f, 1e-5f);
    CHECK_NEAR(mapped(nodes[1])->coord.y, 30.0f, 1e-5f);
}

TEST(strip_table_survives_a_single_point)
{
    const float coords[][2] = { {3.0f, 4.0f} };
    HSVStripSegment segment(nullptr, 0);
    const auto nodes = HSVStripNodeFactory::GenerateFromTable(&segment, 0, coords, 1);
    CHECK_EQ(static_cast<int>(nodes.size()), 1);
    CHECK_NEAR(mapped(nodes[0])->coord.x, 3.0f, 1e-5f);
    CHECK_EQ(HSVStripNodeFactory::MedianSpacing(coords, 1), 0.0f);
}
