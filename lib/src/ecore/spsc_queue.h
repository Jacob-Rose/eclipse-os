// Copyright 2024 | Jake Rose
//
// This file is part of project eclipse-os
// See readme.md for full license details.

#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>

namespace ecore
{
    /* @brief A fixed queue from exactly one thread or core to exactly one
    * other, with no lock: neither side ever waits. Holds `Capacity` items;
    * push() returns false when full.
    *
    *     SpscQueue<Command, 16> commands;
    *     commands.push(command);               // the one producer
    *     while (commands.pop(command)) { }     // the one consumer
    *
    * Uses only atomic loads and stores, which the RP2040's Cortex-M0+ has.
    */
    template <typename T, size_t Capacity>
    class SpscQueue
    {
    public:
        bool push(const T& item)
        {
            const uint32_t tail = written.load(std::memory_order_relaxed);
            if (tail - taken.load(std::memory_order_acquire) >= Capacity)
            {
                return false;
            }
            items[tail % Capacity] = item;
            written.store(tail + 1, std::memory_order_release);
            return true;
        }

        bool pop(T& outItem)
        {
            const uint32_t head = taken.load(std::memory_order_relaxed);
            if (head == written.load(std::memory_order_acquire))
            {
                return false;
            }
            outItem = items[head % Capacity];
            taken.store(head + 1, std::memory_order_release);
            return true;
        }

    private:
        static_assert(Capacity > 0 && (Capacity & (Capacity - 1)) == 0,
                      "a power of two, so the counters can wrap");

        T items[Capacity]{};
        std::atomic<uint32_t> written{0};
        std::atomic<uint32_t> taken{0};
    };
}
