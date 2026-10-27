// Copyright 2024 | Jake Rose
//
// This file is part of project eclipse-os
// See readme.md for full license details.

#include "eclipse_test.h"

#include <ecore/name.h>

using namespace ecore;

TEST(name_hash_is_fnv1a_and_stable)
{
    // published FNV-1a 32-bit vectors; a name sent over the cable as a hash
    // has to come out the same on the host and on the relic
    CHECK_EQ(Name::hashOf("a"), 0xe40c292cu);
    CHECK_EQ(Name::hashOf("foobar"), 0xbf9cf968u);
    CHECK_EQ(Name("foobar").getHash(), Name::hashOf("foobar"));
    CHECK_EQ(Name(std::string("foobar")).getHash(), Name::hashOf("foobar"));
}

TEST(name_equality_is_by_hash_and_empty_is_invalid)
{
    const Name a("parent.child");
    const Name b("parent.child");
    const Name c("parent.other");
    const Name none;

    CHECK(a == b);
    CHECK(a != c);
    CHECK(a.isValid());
    CHECK(!none.isValid());
    CHECK(!Name("").isValid());
    CHECK(none == Name(""));
    CHECK_EQ(a.getText(), std::string("parent.child"));
}

TEST(name_matches_walks_the_dotted_hierarchy)
{
    const Name child("parent.child");
    const Name parent("parent");
    const Name prefix("par");
    const Name deep("parent.child.grandchild");

    CHECK(child.matches(parent));
    CHECK(child.matches(child));
    CHECK(deep.matches(parent));
    CHECK(deep.matches(child));
    CHECK(!parent.matches(child));   // a parent is not its child
    CHECK(!child.matches(prefix));      // boundary match, not prefix
    CHECK(!Name().matches(parent)); // empty matches nothing
    CHECK(!child.matches(Name()));    // and nothing matches empty
}

TEST(name_orders_by_text)
{
    CHECK(Name("alpha") < Name("beta"));
    CHECK(!(Name("beta") < Name("alpha")));
}
