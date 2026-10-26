// Copyright 2024 | Jake Rose
//
// This file is part of project eclipse-os
// See readme.md for full license details.

#pragma once

#include <cstdint>

///
/// Fixed point: the 0..1 ranges a colour is made of, stored as integers so the
/// per-pixel math is integer math. The RP2040 has no FPU and no divide
/// instruction, so everything here is a multiply and a shift; the scale is a
/// power of two for exactly that reason.
///
/// Two domains share one storage type:
///
///   unit  - saturation, value, a blend weight. 0 .. ONE inclusive, clamped.
///   turns - hue, as a fraction of a full turn. 0 .. ONE exclusive, wrapping:
///           ONE is the same angle as 0, and the shortest way between two
///           hues may cross it.
///
/// Every function is constexpr so the math can be pinned by the static_asserts
/// at the bottom of this file and by tests/ on the host build.
///

namespace efp
{
    /// storage type for one channel
    typedef uint16_t fpInt;
    /// what the intermediates are done in. Signed, so a hue delta can go negative.
    typedef int32_t fpWide;

    static constexpr int SHIFT = 14;
    /// 1.0 in fixed point. 16384 leaves four bits of headroom in a uint16.
    static constexpr fpInt ONE = static_cast<fpInt>(1) << SHIFT;
    static constexpr fpInt HALF = ONE / 2;
    static constexpr fpWide WRAP_MASK = ONE - 1;

    // ---- float <-> fixed -----------------------------------------------

    /// 0..ONE -> 0.f..1.f. A multiply, not a divide: soft-float division is
    /// the slowest thing the chip does.
    constexpr float toFloat(fpInt value)
    {
        return static_cast<float>(value) * (1.0f / static_cast<float>(ONE));
    }

    /// Clamped to 0..1, rounded to nearest.
    constexpr fpInt fromUnit(float value)
    {
        value = value < 0.0f ? 0.0f : (value > 1.0f ? 1.0f : value);
        return static_cast<fpInt>(value * static_cast<float>(ONE) + 0.5f);
    }

    /// Wraps a hue in turns onto 0..ONE-1: 1.25 turns is 0.25, -0.25 is 0.75.
    /// Not constexpr-fmod, because fmod is not constexpr until C++23; the
    /// truncating cast does the same job for any sane number of turns.
    constexpr fpInt fromTurns(float turns)
    {
        float fraction = turns - static_cast<float>(static_cast<fpWide>(turns));
        if (fraction < 0.0f)
        {
            fraction += 1.0f;
        }
        return static_cast<fpInt>(static_cast<fpWide>(fraction * static_cast<float>(ONE) + 0.5f) & WRAP_MASK);
    }

    // ---- fixed -> hardware ----------------------------------------------

    /// 0..ONE -> 0..255, rounded. ONE lands exactly on 255, so no clamp.
    constexpr uint8_t to8(fpInt value)
    {
        return static_cast<uint8_t>((static_cast<uint32_t>(value) * 255u + HALF) >> SHIFT);
    }

    /// A hue in turns -> the 16-bit hue NeoPixel's ColorHSV takes, where
    /// 65536 is a full turn. A shift, because both scales are powers of two.
    constexpr uint16_t turnsTo16(fpInt turns)
    {
        return static_cast<uint16_t>((static_cast<uint32_t>(turns) & WRAP_MASK) << (16 - SHIFT));
    }

    // ---- arithmetic -------------------------------------------------------

    /// Any integer onto 0..ONE-1. Two's complement makes the mask right for
    /// negatives too: -1 wraps to ONE-1.
    constexpr fpInt wrap(fpWide value)
    {
        return static_cast<fpInt>(value & WRAP_MASK);
    }

    /// a * b, both unit.
    constexpr fpInt mul(fpInt a, fpInt b)
    {
        return static_cast<fpInt>((static_cast<uint32_t>(a) * b) >> SHIFT);
    }

    /// a at t == 0, b at t == ONE. Unit domain: no wrap.
    ///
    /// The product is at most ONE * ONE, 2^28, so it fits fpWide with room.
    /// Right-shifting a negative is arithmetic on GCC for both targets (and
    /// guaranteed from C++20), so the delta may go either way.
    constexpr fpInt lerp(fpInt a, fpInt b, fpInt t)
    {
        return static_cast<fpInt>(a + (((static_cast<fpWide>(b) - a) * t) >> SHIFT));
    }

    /// The same, for hues: takes the short way round the circle, so a blend
    /// from 350 degrees to 10 passes through 0, not 180.
    constexpr fpInt lerpTurns(fpInt a, fpInt b, fpInt t)
    {
        fpWide delta = static_cast<fpWide>(b) - a;
        if (delta > HALF)
        {
            delta -= ONE;
        }
        else if (delta < -static_cast<fpWide>(HALF))
        {
            delta += ONE;
        }
        return wrap(a + ((delta * t) >> SHIFT));
    }

    /// a + b, unit domain, held at ONE.
    constexpr fpInt addClamped(fpInt a, fpInt b)
    {
        const fpWide sum = static_cast<fpWide>(a) + b;
        return static_cast<fpInt>(sum > ONE ? ONE : sum);
    }

    // ---- the contract, checked at compile time ------------------------------

    static_assert(ONE == 16384, "the scale is 2^14");
    static_assert(fromUnit(0.0f) == 0 && fromUnit(1.0f) == ONE, "unit endpoints");
    static_assert(fromUnit(-3.0f) == 0 && fromUnit(7.0f) == ONE, "unit clamps");
    static_assert(fromUnit(0.5f) == HALF, "unit midpoint");
    static_assert(fromTurns(0.0f) == 0 && fromTurns(1.0f) == 0, "a full turn is zero");
    static_assert(fromTurns(0.25f) == ONE / 4 && fromTurns(1.25f) == ONE / 4, "turns wrap up");
    static_assert(fromTurns(-0.25f) == ONE - ONE / 4, "turns wrap down");
    static_assert(to8(0) == 0 && to8(ONE) == 255 && to8(HALF) == 128, "8-bit endpoints round");
    static_assert(turnsTo16(0) == 0 && turnsTo16(HALF) == 32768, "16-bit hue is a shift");
    static_assert(wrap(-1) == ONE - 1 && wrap(ONE) == 0, "wrap on both edges");
    static_assert(mul(ONE, ONE) == ONE && mul(HALF, HALF) == ONE / 4, "mul");
    static_assert(lerp(0, ONE, HALF) == HALF && lerp(ONE, 0, HALF) == HALF, "lerp both directions");
    static_assert(lerp(100, 300, 0) == 100 && lerp(100, 300, ONE) == 300, "lerp endpoints");
    static_assert(lerpTurns(0, ONE / 4, HALF) == ONE / 8, "short arc, no seam");
    static_assert(lerpTurns(ONE - ONE / 8, ONE / 8, HALF) == 0, "short arc across the seam");
    static_assert(lerpTurns(ONE / 8, ONE - ONE / 8, HALF) == 0, "short arc across the seam, other way");
    static_assert(addClamped(HALF, HALF) == ONE && addClamped(ONE, ONE) == ONE, "addClamped");
}
