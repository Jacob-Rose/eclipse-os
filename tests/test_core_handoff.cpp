// Copyright 2024 | Jake Rose
//
// This file is part of project eclipse-os
// See readme.md for full license details.

#include "eclipse_test.h"

#include <ecore/backoff.h>
#include <ecore/spsc_queue.h>

#include <cstdint>
#include <thread>

using namespace ecore;

TEST(backoff_is_due_until_something_fails)
{
    Backoff retry;
    retry.setBase(5000);
    CHECK(retry.due(0));
    retry.failed(1000);
    CHECK(!retry.due(5999));
    CHECK(retry.due(6000));
}

TEST(backoff_doubles_up_to_its_cap)
{
    Backoff retry;
    retry.setBase(5000);
    retry.setCap(60000);

    const uint32_t expected[] = {5000, 10000, 20000, 40000, 60000, 60000};
    uint32_t now = 0;
    for (uint32_t wait : expected)
    {
        CHECK_EQ(retry.nextWait(), wait);
        retry.failed(now);
        CHECK(!retry.due(now + wait - 1));
        CHECK(retry.due(now + wait));
        now += wait;
    }
}

TEST(backoff_starts_over_after_a_success)
{
    Backoff retry;
    retry.setBase(5000);
    retry.failed(0);
    retry.failed(5000);
    retry.succeeded();
    CHECK(retry.due(5001));
    CHECK_EQ(retry.nextWait(), 5000u);
}

TEST(backoff_survives_the_clock_wrapping)
{
    Backoff retry;
    retry.setBase(5000);
    const uint32_t nearTheEnd = 0xFFFFFFFFu - 1000u;
    retry.failed(nearTheEnd);
    CHECK(!retry.due(nearTheEnd + 4999u));
    CHECK(retry.due(nearTheEnd + 5000u));
}

TEST(backoff_does_not_overflow_a_huge_cap)
{
    Backoff retry;
    retry.setBase(0x80000000u);
    retry.setCap(0xFFFFFFFFu);
    retry.failed(0);
    retry.failed(0);
    CHECK_EQ(retry.nextWait(), 0xFFFFFFFFu);
}

TEST(spsc_queue_keeps_order_and_refuses_when_full)
{
    SpscQueue<int, 4> queue;
    for (int i = 0; i < 4; ++i)
    {
        CHECK(queue.push(i));
    }
    CHECK(!queue.push(99));

    int item = -1;
    for (int i = 0; i < 4; ++i)
    {
        CHECK(queue.pop(item));
        CHECK_EQ(item, i);
    }
    CHECK(!queue.pop(item));
}

TEST(spsc_queue_wraps_past_its_counters)
{
    SpscQueue<int, 4> queue;
    int item = 0;
    for (int i = 0; i < 1000; ++i)
    {
        CHECK(queue.push(i));
        CHECK(queue.pop(item));
        CHECK_EQ(item, i);
    }
}

TEST(spsc_queue_hands_everything_across_threads_in_order)
{
    SpscQueue<uint32_t, 16> queue;
    constexpr uint32_t kCount = 200000;

    std::thread producer([&queue] {
        for (uint32_t i = 0; i < kCount; )
        {
            if (queue.push(i))
            {
                ++i;
            }
        }
    });

    uint32_t expected = 0;
    bool inOrder = true;
    while (expected < kCount)
    {
        uint32_t item = 0;
        if (queue.pop(item))
        {
            inOrder = inOrder && item == expected;
            ++expected;
        }
    }
    producer.join();

    CHECK(inOrder);
    CHECK_EQ(expected, kCount);
}
