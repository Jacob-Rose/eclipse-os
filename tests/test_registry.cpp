// Copyright 2024 | Jake Rose
//
// This file is part of project eclipse-os
// See readme.md for full license details.

#include "eclipse_test.h"

#include <eanim/registry.h>
#include <eio/host_relic.h>

using namespace eanim;

namespace
{
    struct Solid : public GeneratorHSV
    {
        int inits{0};
        void init() { ++inits; }
        void render(eio::HSVStripNode*, ecore::HSV& inOutColor) const override { inOutColor = ecore::HSV(0.0f, 1.0f, 1.0f); }
        void tick(float) override {}
    };
}

TEST(registry_makes_looks_by_name_and_the_last_word_wins)
{
    GeneratorRegistry looks;
    CHECK_EQ(looks.size(), 0u);
    CHECK(!looks.has("solid"));
    CHECK(looks.make("solid") == nullptr);

    looks.add("solid", []() { return std::make_shared<Solid>(); });
    looks.add("zed", []() { return std::make_shared<Solid>(); });
    looks.add("alpha", []() { return std::make_shared<Solid>(); });
    CHECK(looks.has("solid"));
    CHECK_EQ(looks.size(), 3u);
    CHECK_EQ(looks.names()[0], std::string("alpha"));   // sorted
    CHECK_EQ(looks.names()[2], std::string("zed"));

    std::shared_ptr<GeneratorHSV> made = looks.make("solid");
    CHECK(made != nullptr);
    ecore::HSV color;
    made->render(nullptr, color);
    CHECK_NEAR(color.getHueFloat(), 0.0f, 0.1f);

    // a second registration of a name replaces the first
    looks.add("solid", []() { auto s = std::make_shared<Solid>(); s->init(); return s; });
    CHECK_EQ(looks.size(), 3u);
    CHECK_EQ(std::static_pointer_cast<Solid>(looks.make("solid"))->inits, 1);

    looks.add("broken", nullptr);
    CHECK(looks.make("broken") == nullptr);
    looks.clear();
    CHECK_EQ(looks.size(), 0u);

    GeneratorRegistry& shared = GeneratorRegistry::shared();
    CHECK(&shared == &GeneratorRegistry::shared());
}

TEST(host_relic_io_builds_a_strip_the_caller_shapes)
{
    eio::HostRelicIO io;
    io.build(4, 7, [](eio::HSVStripSegment* segment, size_t idx) {
        auto node = std::make_shared<eio::HSVStripNode_Mapped2D>(segment, static_cast<int>(idx));
        node->coord = ecore::Coordinate(static_cast<float>(idx), 2.0f);
        return node;
    });
    CHECK(io.getStrip() != nullptr);
    CHECK_EQ(io.getNodes().size(), 4u);
    CHECK_NEAR(io.getNodes()[3]->coord.x, 3.0f, 0.0f);
    CHECK_EQ(io.strip_segments.size(), 1u);
    CHECK(io.strip_segments.find(7) != io.strip_segments.end());
    CHECK(io.getScreenDrawer() == nullptr);

    // with no shaper every node is a plain mapped node; a rebuild starts over
    io.build(2, 1);
    CHECK_EQ(io.getNodes().size(), 2u);
    CHECK_EQ(io.strip_segments.size(), 1u);
    CHECK(io.strip_segments.find(1) != io.strip_segments.end());
}
