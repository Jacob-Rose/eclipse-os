// Copyright 2024 | Jake Rose
//
// This file is part of project eclipse-os
// See readme.md for full license details.

#pragma once

#include <algorithm>
#include <cstdint>

namespace ecore
{
    /* @brief When to try a failing thing again: the wait doubles from a base
    * up to a cap, and a success puts it back. Times are a millisecond clock
    * that may wrap, such as millis().
    *
    *     Backoff retry;
    *     retry.setBase(5000);
    *     if (retry.due(now))
    *     {
    *         if (connect()) retry.succeeded();
    *         else           retry.failed(now);
    *     }
    */
    class Backoff
    {
    public:
        /// The first wait. Also where a success puts the wait back to.
        void setBase(uint32_t milliseconds)
        {
            base = milliseconds;
            wait = milliseconds;
        }

        /// The longest wait.
        void setCap(uint32_t milliseconds) { cap = milliseconds; }

        /// True when an attempt may start. True from the start, and after a success.
        bool due(uint32_t now) const
        {
            return !waiting || static_cast<int32_t>(now - nextAttempt) >= 0;
        }

        void failed(uint32_t now)
        {
            nextAttempt = now + std::min(wait, cap);
            waiting = true;
            wait = wait > cap / 2 ? cap : wait * 2;
        }

        void succeeded()
        {
            wait = base;
            waiting = false;
        }

        /// What the next failure will wait, in milliseconds.
        uint32_t nextWait() const { return std::min(wait, cap); }

    private:
        uint32_t base{5000};
        uint32_t cap{60000};
        uint32_t wait{5000};
        uint32_t nextAttempt{0};
        bool waiting{false};
    };
}
