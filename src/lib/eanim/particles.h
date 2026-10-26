// Copyright 2026 | Jake Rose
//
// This file is part of project eclipse-os
// See readme.md for full license details.

#pragma once

#include <cstdint>
#include <vector>

#include "../ecore/coord.h"

namespace eanim
{
    /* @brief One particle: a position and motion in the 2d space, plus the
    * life bookkeeping.
    *
    * What a particle *means* - its colour, its shape on the pixels, what
    * happens when it dies - belongs to the look that owns the system. This
    * carries only what every moving point needs.
    */
    struct Particle
    {
        bool bAlive{false};

        ecore::Coordinate position;
        ecore::Coordinate velocity;

        /// how far the particle's glow reaches, in stage units
        float radius{1.0f};

        float age{0.0f};
        /// seconds to live; zero or less means until the look kills it
        float lifetime{0.0f};

        /// stable per-particle noise seed, set at spawn - for hashes that
        /// must hold still across the particle's whole life
        uint32_t seed{0};
    };


    /* @brief A fixed pool of particles: spawn, integrate, expire.
    *
    * Fixed capacity and no allocation after construction, because this
    * compiles for the microcontrollers - same deal as AutomationCurve. The
    * system owns motion: tick() integrates velocity, ages everyone, and
    * expires the mortal. Everything else is the owning look's business,
    * which is what keeps this simple enough to be worth sharing.
    *
    * glowAt() is the one rendering favour it does: the soft radial falloff
    * of a particle at a point, which is what makes a particle a *position
    * in the plane* - readable by a ring's arc and an obelisk's runs alike -
    * rather than an index on some strip.
    */
    class ParticleSystem
    {
    public:
        explicit ParticleSystem(int inCapacity);

        /// integrates positions, ages everyone, expires the mortal
        void tick(float deltaTime);

        /// a dead slot brought to life, or nullptr when the pool is full.
        /// The slot comes back zeroed except for a fresh seed.
        Particle* spawn();

        void clear();

        std::vector<Particle>& all() { return particles; }
        const std::vector<Particle>& all() const { return particles; }

        int aliveCount() const;

        /* @brief The particle's glow at a point: 1 at the centre, 0 at the
        * radius, squared so the edge lands softly. Zero for the dead. */
        static float glowAt(const Particle& particle, const ecore::Coordinate& at);

    private:
        std::vector<Particle> particles;
        uint32_t spawned{0};
    };
}
